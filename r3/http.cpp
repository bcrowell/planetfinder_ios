/*
*  http
*/

/* 
Copyright (c) 2010 Cass Everitt
All rights reserved.

Redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the following
disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials
provided with the distribution.

* The names of contributors to this software may not be used
to endorse or promote products derived from this software
without specific prior written permission. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
POSSIBILITY OF SUCH DAMAGE. 


Cass Everitt
*/

#include "r3/http.h"

#include "r3/output.h"
#include "r3/parse.h"
#include "r3/socket.h"

#include <assert.h>

#include <deque>
#include <vector>
#include <map>

using namespace std;
using namespace r3;


namespace {


	enum UrlProtocolEnum {
		UrlProtocol_INVALID,
		UrlProtocol_HTTP,
		UrlProtocol_MAX
	};

	struct UniformResourceLocator {
		UniformResourceLocator( const string & urlString ) : protocol( UrlProtocol_INVALID ), port( 80 ) {
			string url = urlString;

			// parse protocol
			if ( url.find( "http://" ) != string::npos ) {
				protocol = UrlProtocol_HTTP;
				url.erase( 0, 7 );
			} else {
				return;
			}

			// get hostname
			while( url.size() != 0 && url[0] != '/' && url[0] != ':')
			{
				hostname.push_back( url[0] );
				url.erase( 0, 1 );
			}

			if ( url.size() == 0 ) {
				return;
			}

			// port (optional)
			if ( url[0] == ':' )
			{
				url.erase( 0, 1 );
				int p = 0;
				while( url.size() != 0 && url[0] != '/' ) {
					ushort digit = url[0] - '0';
					if ( digit > 9 ) {
						protocol = UrlProtocol_INVALID;
						return;
					}
					p = p * 10 + digit;
				}
				port = p;
				if ( p != int( port ) ) {
					protocol = UrlProtocol_INVALID;
					return;
				}
			}

			// path
			path = url;
			if ( path.size() == 0 ) {
				path = "/";
			}
		}

		UrlProtocolEnum protocol;
		string hostname;
		ushort port;
		string path;
		// todo: add args
		// todo: handle escapes?
	};			

	// input adapter to avoid reading a char?
	struct InputStream {
		InputStream( Socket &socket ) : sock( socket ) {}
		// get data into the fifo
		void TopUp() {
			if ( (int)fifo.size() == 0 ) {
				char data[512];
				int size = sock.ReadPartial( data, ARRAY_ELEMENTS( data ) );
				fifo.insert( fifo.end(), data, data + size );
			}
		}
		bool AtEnd() {
			TopUp();
			return (int)fifo.size() == 0;
		}
		char GetChar() {
			TopUp();
			assert( fifo.size() > 0 );
			char r = fifo.front();
			fifo.pop_front();
			return r;
		}
		void PutChar( char c ) {
			fifo.push_front( c );
		}
		int Read( int bytes, vector<uchar> & data ) {
			int origBytes = bytes;
			int pos = data.size();
			data.resize( pos + bytes );
			while( bytes > 0 && fifo.size() > 0 ) {
				data[pos] = fifo.front();
				pos++;
				bytes--;
				fifo.pop_front();
			}
			if ( bytes > 0 ) {
				if ( sock.Read( (char *)&data[ pos ], bytes ) ) {
					bytes = 0;
				}
			} 
			return origBytes - bytes;
		}
		Socket & sock;
		deque<char> fifo;
	};

	struct HttpResponse {
		HttpResponse( InputStream & is ) : code( 0 ) {
			char prevc = 0;
			int line = 0;
			string lineStr;
			vector<string> kv; // even entries are "key", odd entries are "value"
			while ( is.AtEnd() == false ) {
				char c = is.GetChar();
				if ( c == '\r' ) {
					continue;
				}
				if ( c == '\n' ) {
					// process line
					if ( line == 0 ) {
						vector<Token> tokens = TokenizeString( lineStr.c_str() );
						if ( tokens.size() < 3 ) {
							return;
						}
						protocol = tokens[0].valString;
						if ( protocol.find( "HTTP/") == string::npos ) {
							protocol = "";
							return;
						}
						code = tokens[1].valNumber;
						for ( int i = 2; i < (int)tokens.size(); i++ ) {
							reason += tokens[i].valString;
							reason += ' ';
						}
					} else {
						// header lines
						if ( lineStr.size() ) {
							if ( ( lineStr[0] == ' ' || lineStr[0] == '\t' ) && kv.size() > 1 ) { // append
								kv.back() += lineStr;
							} else { // regular line
								size_t pos = lineStr.find( ':', 0 );
								if ( pos != string::npos ) {
									string k = LowerCase( lineStr.substr( 0, pos ) );
									do {
										pos++;
									} while( lineStr[ pos ] == ' ' );
									string v = lineStr.substr( pos );
									kv.push_back( k );
									kv.push_back( v );
								}
							}
						}
					}
					lineStr.clear();
					line++;
				}
				if ( c == '\n' && prevc == '\n' ) {
					break;
				}
				if ( c != '\n' ) {
					lineStr.push_back( c );
				}
				prevc = c;
			}
			if ( ( kv.size() & 1 ) == 0 ) {
				for ( int i = 0; i < (int)kv.size(); i+=2 ) {
					header[ kv[ i ] ] = kv[ i + 1 ];
				}
			}
		}
		bool HasKey( const string & k ) {
			string key = LowerCase( k );
			return header.count( key ) != 0;			
		}
		
		int GetInt( const string & k ) {
			string key = LowerCase( k );
			if ( HasKey( key ) ) {
				float f;
				StringToFloat( header[ key ].c_str(), f );
				return f;
			}
			return 0;
		}
		
		string GetString( const string & k ) {
			string key = LowerCase( k );
			if ( HasKey( key ) ) {
				return header[ key ].c_str();
			}
			return string();
		}
		
		string protocol;
		int code;
		string reason;
		map< string, string > header;
	};

}

namespace r3 {

	bool UrlReadToMemory( const string & urlString, vector< uchar > & data ) {
		data.clear();
		UniformResourceLocator u( urlString );
		if ( u.protocol == UrlProtocol_INVALID ) {
			return false;
		}

		uint ip = GetIpAddress( u.hostname );
		Socket sock;
		if ( sock.Connect( ip, u.port ) == false ) {
			return false;
		}

		char buf[512];
		int sz;
		r3Sprintf( buf, "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n", u.path.c_str(), u.hostname.c_str(), u.port );
		sz = strlen( buf );
		Output( "Sending http request (%d chars): %s", sz, buf );
		sock.Write( buf, sz );
		InputStream is( sock );
		HttpResponse resp ( is );

		Output( "%s %d %s", resp.protocol.c_str(), resp.code, resp.reason.c_str() );
		for( map<string, string>::iterator it = resp.header.begin(); it != resp.header.end(); ++it ) {
			Output( "    %s: %s", it->first.c_str(), it->second.c_str() );
		}
		
		int bytes = resp.GetInt("Content-Length");
		if ( bytes > 0 ) { // not chunked - single payload
			int r = is.Read( bytes, data );
			Output( "content length = %d, and read = %d", bytes, r );
		} else if ( resp.GetString( "Transfer-Encoding" ).find( "chunked" )  != string::npos ) { // chunks
			Output( "chunked output, abandon hope! (or implement me)");
		}
		

		sock.Disconnect();

		return true;
	}

}
