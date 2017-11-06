/*
 *  star3map
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

#include "star3map.h"
#include "satellite.h"
#include "button.h"

#include "r3/command.h"
#include "r3/common.h"
#include "r3/draw.h"
#include "r3/filesystem.h"
#include "r3/http.h"
#include "r3/model.h"
#include "r3/modelobj.h"
#include "r3/output.h"
#include "r3/thread.h"
#include "r3/time.h"

#include "r3/var.h"

#include <map>

using namespace std;
using namespace star3map;
using namespace r3;

extern VarFloat r_fov;
extern VarInteger r_windowWidth;
extern VarInteger r_windowHeight;

extern VarFloat app_latitude;
extern VarFloat app_longitude;
extern VarFloat app_phaseEarthRotation;
extern VarBool app_cull;

VarBool app_pauseAging( "app_pauseAging", "stop aging for dynamic objects - usually for screenshots", 0, false );
VarInteger app_debugLabels( "app_debugLabels", "draw debugging info for labels", 0, 0 );
VarBool app_useCompass( "app_useCompass", "use the compass for pointing the phone around the sky", 0, false );
VarBool app_changeLocation( "app_changeLocation", "change current location when moving in globeView", 0, false );
VarBool app_useCoreLocation( "app_useCoreLocation", "use the location services to get global position", 0, false );
VarBool app_showSatellites( "app_showSatellites", "use TLE satellite data to show satellites", 0, false );
VarString app_satelliteFile( "app_satelliteFile", "file to use for satellite data", 0, "visual.txt" );
VarInteger app_maxSatellites( "app_maxSatellites", "maximum number of satellites to display", 0, 40 );

VarFloat app_inputDrag( "app_inputDrag", "drag factor on input for inertia effect", 0, .9 );

VarFloat app_debugPhase( "app_debugPhase", "phase adjustment", 0, 0.0f );

extern VarFloat app_scale;
extern VarFloat app_starScale;

bool GotCompassUpdate = false;
bool GotLocationUpdate = false;

extern void UpdateLatLon();
extern Matrix4f platformOrientation;
Matrix4f orientation;
Matrix4f manualOrientation;

VarFloat app_manualPhi( "app_manualPhi", "phi for manual orientation", 0, 0 );
VarFloat app_manualTheta( "app_manualTheta", "theta for manual orientation", 0, 0 );

vector< Sprite > stars;
vector< Sprite > solarsystem;
vector< Lines > constellations;
vector< Button *> buttons;

namespace star3map {
	Matrix4f RotateTo( const Vec3f & direction );
}

#if IPHONE
extern void OpenURL( const char *url );
#else
void OpenURL( const char *url) {
}
#endif

namespace {

	Model *hemiModel;
	Model *sphereModel;
	Model *starsModel;
	Texture2D *hemiTex;
	Texture2D *nTex;
	Texture2D *sTex;
	Texture2D *eTex;
	Texture2D *wTex;
	Texture2D *testTex;
	Texture2D *earthTex;
	Texture2D *satTex;
	ToggleButton *toggleCompass;
	PushButton *viewGlobe;
	PushButton *viewStars;
	ToggleButton *toggleChangeLocation;
	ToggleButton *toggleCoreLocation;
	ToggleButton *toggleSatellites;
#if APP_star3free
	PushButton *star3mapAppStore;
#endif
	bool GotSatelliteData = false;
	vector<Satellite> satellite;
	
	enum AppModeEnum {
		AppMode_INVALID,
		AppMode_ViewStars,
		AppMode_ViewGlobe,
		AppMode_MAX
	};
	
	AppModeEnum appMode = AppMode_ViewStars;
	
	void UpdateManualOrientation();
	
	struct StarVert {
		Vec3f pos;
		uchar c[4];
		Vec2f tc;
	};
	
	float magToDiam[] = { 2.0f, 1.5f, 1.25f, 1.0f, .85f, .75f, .5f };
	
	float GetSpriteDiameter( float magnitude ) {
		int mag = max( 0, min( 6, (int)magnitude ) );
		return magToDiam[ mag ];
	}
	
	float magToColor[] = { 1.0f, .9f, .7f, .5f, .3f, .2f, .1f };
	
	float GetSpriteColorScale( float magnitude ) {
		int mag = max( 0, min( 6, (int)magnitude ) );
		return magToColor[ mag ];
	}
	
	struct ReadUrlThread : public r3::Thread {
		ReadUrlThread( const string & inUrl ) : url( inUrl ) { }
		string url;
		vector<uchar> urldata;
		virtual void Run() {
			UrlReadToMemory( url, urldata );
		}
	};
	
	
	
	bool initialized = false;
	ReadUrlThread *twoLineElements;
	void Initialize() {
		if ( initialized ) {
			return;
		}
		// start async reads
		twoLineElements = new ReadUrlThread("http://celestrak.com/NORAD/elements/" + app_satelliteFile.GetVal() );
		twoLineElements->Start();

		// construct hemi model
		{			
			hemiModel = new Model( "hemi" );
			float data[ 37 * 2 * 5 ];
			float *d = data;
			for( int i = 0; i <= 36; i++ ) {
				Vec3f p0 = SphericalToCartesian( 1.0f, 0.0f, ( 2.0 * R3_PI * i ) / 36.f );
				d[0] = p0.x;
				d[1] = p0.y;
				d[2] = p0.z;
				d[3] = 0.5f;
				d[4] = 0.0f;
				d+=5;
				Vec3f p1 = SphericalToCartesian( 1.0f, ToRadians( 45.0f ), ( 2.0 * R3_PI * i ) / 36.f );
				d[0] = p1.x;
				d[1] = p1.y;
				d[2] = p1.z;
				d[3] = 0.5f;
				d[4] = 0.5f;
				d+=5;
			}
			VertexBuffer & vb = hemiModel->GetVertexBuffer();
			vb.SetVarying( Varying_PositionBit | Varying_TexCoord0Bit );
			vb.SetData( 37 * 2 * 5 * sizeof( float ), data );
			hemiModel->SetPrimitive( Primitive_TriangleStrip );
		} 
		{
			sphereModel = new Model( "sphere" );
			vector<float> data;
			int wedges = 36;
			int slices = 18;
			for ( int j = 0; j <= slices; j++ ) {
				float t = float( j ) / slices;
				float phi = R3_PI * ( t - 0.5 );
				for( int i = 0; i <= wedges; i++ ) {
					float s = float( i ) / wedges;
					Vec3f p = SphericalToCartesian( 1.0f, phi, 2.0 * R3_PI * s );
					data.push_back( p.x );
					data.push_back( p.y );
					data.push_back( p.z );
					data.push_back( s + 0.5f ); // gmt is at s = 0.5 for the earth map texture
					data.push_back( t );
				}
			}
			vector<ushort> index;
			int pitch = wedges + 1;
			for ( int j = 0; j < slices; j++ ) {
				for( int i = 0; i < wedges; i++ ) {
					// lower right tri
					index.push_back( ( j + 0 ) * pitch + ( i + 0 ) );
					index.push_back( ( j + 0 ) * pitch + ( i + 1 ) );
					index.push_back( ( j + 1 ) * pitch + ( i + 1 ) );
					// upper left tri
					index.push_back( ( j + 0 ) * pitch + ( i + 0 ) );
					index.push_back( ( j + 1 ) * pitch + ( i + 1 ) );					
					index.push_back( ( j + 1 ) * pitch + ( i + 0 ) );
				}
			}
			VertexBuffer & vb = sphereModel->GetVertexBuffer();
			vb.SetVarying( Varying_PositionBit | Varying_TexCoord0Bit );
			vb.SetData( (int)data.size() * sizeof( float ), & data[0] );
			IndexBuffer & ib = sphereModel->GetIndexBuffer();
			ib.SetData( (int)index.size() * sizeof( ushort ), & index[0] );
			sphereModel->SetPrimitive( Primitive_Triangles );
		}
		{
			starsModel = new Model( "stars" );
			vector< StarVert > data;
			for ( int i = 0; i < (int)stars.size(); i++ ) {
				Sprite & s = stars[i];
				if ( s.magnitude > 4 ) {
					continue;
				}
				Vec4f c = s.color * GetSpriteColorScale( s.magnitude ) * 255.f;
				Matrix4f mRot = RotateTo( s.direction );
				Matrix4f mScale;
				float sc = s.scale * GetSpriteDiameter( s.magnitude ) * app_starScale.GetVal() * app_scale.GetVal();
				mScale.SetScale( Vec3f( sc, sc, 1 ) );
				Matrix4f mTrans;
				mTrans.SetTranslate( Vec3f( 0, 0, -1 ) );
				Matrix4f m = mRot * mScale * mTrans;
				StarVert v;
				v.c[0] = c.x; v.c[1] = c.y; v.c[2] = c.z; v.c[3] = c.w;
				v.pos = m * Vec3f( -10, -10, 0 );
				v.tc = Vec2f( 0, 0 );
				data.push_back( v );
				v.pos = m * Vec3f(  10, -10, 0 );
				v.tc = Vec2f( 1, 0 );
				data.push_back( v );
				v.pos = m * Vec3f(  10,  10, 0 );
				v.tc = Vec2f( 1, 1 );
				data.push_back( v );
				v.pos = m * Vec3f( -10,  10, 0 );
				v.tc = Vec2f( 0, 1 );
				data.push_back( v );				
			}
			VertexBuffer & vb = starsModel->GetVertexBuffer();
			vb.SetVarying( Varying_PositionBit | Varying_ColorBit | Varying_TexCoord0Bit );
			vb.SetData( (int)data.size() * sizeof( StarVert ), & data[0] );
			starsModel->SetPrimitive( Primitive_Quads );
			
		}
		
		
		hemiTex = CreateTexture2DFromFile( "hemi.png", TextureFormat_RGBA );
		nTex = CreateTexture2DFromFile( "n.png", TextureFormat_RGBA );
		sTex = CreateTexture2DFromFile( "s.png", TextureFormat_RGBA );
		eTex = CreateTexture2DFromFile( "e.png", TextureFormat_RGBA );
		wTex = CreateTexture2DFromFile( "w.png", TextureFormat_RGBA );
		testTex = CreateTexture2DFromFile( "test.jpg", TextureFormat_RGB );
		earthTex = CreateTexture2DFromFile( "earth.png", TextureFormat_RGB );
		satTex = CreateTexture2DFromFile( "spacestation.png", TextureFormat_RGBA );
		
		toggleCompass = new ToggleButton( "crose64.png", "app_useCompass" );
		viewGlobe = new PushButton( "globe64.png", "setAppMode viewGlobe" );
		viewStars = new PushButton( "viewstars64.png", "setAppMode viewStars" );
		toggleChangeLocation = new ToggleButton( "arrows.png", "app_changeLocation" );
		toggleCoreLocation = new ToggleButton( "satnav.png", "app_useCoreLocation" );
		toggleSatellites = new ToggleButton( "spacestation.png", "app_showSatellites" );
		
#if APP_star3free
		star3mapAppStore = new PushButton( "star3map_icon.png", "goToAppStore" );
#endif		
		
		UpdateManualOrientation();
		
		ReadSatelliteData( "satellite/" + app_satelliteFile.GetVal() );		
		ComputeSatellitePositions( satellite );

		initialized = true;
	}
	
	void PlaceButtons() {
		buttons.clear();
		int border = 8;
		int buttonWidth = 32;		
		int y = r_windowHeight.GetVal() - buttonWidth - border;
		int x = border;
		if ( appMode == AppMode_ViewStars ) {
#if APP_star3free
			star3mapAppStore->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
			buttons.push_back( star3mapAppStore );
			x += buttonWidth + border;
#endif
			viewGlobe->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
			buttons.push_back( viewGlobe );
			x += buttonWidth + border;
			if ( GotCompassUpdate ) {
				toggleCompass->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
				buttons.push_back( toggleCompass );				
				x += buttonWidth + border;
			}
		} else if ( appMode == AppMode_ViewGlobe ) {
			viewStars->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
			buttons.push_back( viewStars );
			x += buttonWidth + border;
			toggleChangeLocation->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
			buttons.push_back( toggleChangeLocation );
			x += buttonWidth + border;
			toggleCoreLocation->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
			buttons.push_back( toggleCoreLocation );				
			x += buttonWidth + border;
		}
		if ( satellite.size() > 0 ) {
			toggleSatellites->bounds = Bounds2f( x, y, x + buttonWidth, y + buttonWidth );
			buttons.push_back( toggleSatellites );
			x += buttonWidth + border;
		}
		
	}
	
	struct DrawNonOverlappingStrings {
		vector< OrientedBounds2f > reserved;
		vector< OrientedBounds2f > obs;
		
		bool CanDrawString( const string & str, const Vec3f & direction, const Vec3f & lookDir, float limit ) {
			if ( lookDir.Dot( direction ) < limit ) {
				return false;
			}
			OrientedBounds2f ob = StringBounds( str, direction );
			for ( int i = 0; i < (int)obs.size(); i++ ) {
				if ( Intersect( ob, obs[ i ] ) ) {
					return false; // intersected, so don't draw this one
				}
			}
			return true;
		}
		
		void ReserveString( const string & str, const Vec3f & direction, const Vec3f & lookDir, float limit ) {
			if ( lookDir.Dot( direction ) < limit ) {
				return;
			}
			OrientedBounds2f ob = StringBounds( str, direction );
			reserved.push_back( ob );
		}

		void DrawString( const string & str, const Vec3f & direction, const Vec3f & lookDir, float limit ) {
			if ( lookDir.Dot( direction ) < limit ) {
				return;
			}
			
			OrientedBounds2f ob = StringBounds( str, direction );
			if ( ob.empty ) {
				return;
			}
				
			for ( int i = 0; i < (int)obs.size(); i++ ) {
				if ( Intersect( ob, obs[ i ] ) ) {
					return; // intersected, so don't draw this one
				}
			}
			for ( int i = 0; i < (int)reserved.size(); i++ ) {
				if ( Intersect( ob, reserved[ i ] ) ) {
					return; // intersected, so don't draw this one
				}
			}
			obs.push_back( ob );
			::DrawString( str, direction );
			if ( app_debugLabels.GetVal() ) {
				if ( app_debugLabels.GetVal() > 1 ) {
					float len = 0;
					for ( int i = 0; i < 4; i++ ) {
						len = max( len, ( ob.vert[ i ] - ob.vert[ (i+1)%4 ] ).Length() );
					}
					if ( len > 10 ) {
						Output( "bounds %d %s: ( %f, %f ), ( %f, %f ), ( %f, %f ), ( %f, %f )",
							   (int)obs.size(), str.c_str(),
							   ob.vert[0].x, ob.vert[0].y,
							   ob.vert[1].x, ob.vert[1].y,
							   ob.vert[2].x, ob.vert[2].y,
							   ob.vert[3].x, ob.vert[3].y );					
					}
				}
				PushTransform();
				ClearTransform();
				SetColor( Vec4f( 1, 1, 0, 1 ) );
				ImVarying( 0 );
				ImBegin( Primitive_LineStrip );
				ImVertex( ob.vert[0] );
				ImVertex( ob.vert[1] );
				ImVertex( ob.vert[2] );
				ImVertex( ob.vert[3] );
				ImVertex( ob.vert[0] );
				ImEnd();
				PopTransform();
			}
		}
	};

	const float RampUpTime = .5f;
	const float RampDownTime = 1.0f;
	
	struct DynamicRenderable {
		enum DState {
			DState_Invalid,
			DState_RampUp,
			DState_Hold,
			DState_RampDown,
			DState_Terminate,
			DState_MAX
		};
		DynamicRenderable() {}
		DynamicRenderable( const Vec3f & lDir, const Vec3f & lLookDir, float lLimit, const Vec4f & lColor, float lDuration ) 
		: direction( lDir ), lookDir( lLookDir ), limit( lLimit ), color( lColor ), duration( lDuration ), state( DState_RampUp ) {
			timeStamp = GetSeconds();
			currAlpha = 0.0f;
		} 
		Vec3f direction;
		Vec3f lookDir;
		float limit;
		Vec4f color;
		float currAlpha;
		float duration;
		DState state;
		float timeStamp;
		float lastSeen;
		void age() {
			float currTime = GetSeconds();
			float delta = currTime - timeStamp;
			switch ( state ) {
				case DState_RampUp:
					if ( delta > RampUpTime ) {
						timeStamp += RampUpTime;
						state = DState_Hold;
						delta = RampUpTime;
					}
					currAlpha = ( delta / RampUpTime ) * color.w;
					break;
				case DState_Hold:
					if ( delta > duration ) {
						timeStamp += duration;
						state = DState_RampDown;
					}
					break;
				case DState_RampDown:
					if ( delta > RampDownTime ) {
						timeStamp += RampUpTime;
						state = DState_Terminate;
						delta = RampDownTime;
					}
					currAlpha = ( 1.0f - delta / RampDownTime ) * color.w;
					break;
				case DState_Terminate:
					break;
				default:
					Output( "Something has gone wrong." );
					break;
			}
		}
		void seen() {
			lastSeen = GetSeconds();
		}
	};
	
	struct DynamicLabel : public DynamicRenderable {
		DynamicLabel() {}
		DynamicLabel( const string & lName, const Vec3f & lDir, const Vec3f & lLookDir, float lLimit, const Vec4f & lColor, float lDuration ) 
		: DynamicRenderable( lDir, lLookDir, lLimit, lColor, lDuration ), name( lName ) {
			timeStamp = GetSeconds();
			currAlpha = 0.0f;
		} 
		string name;
		void render( DrawNonOverlappingStrings & nos ) {
			if ( state != DState_Terminate ) {
				SetColor( Vec4f( color.x, color.y, color.z, currAlpha ) );
				nos.DrawString( name, direction, lookDir, limit );
			}
		}
	};
	
	map< string, DynamicLabel > dynamicLabels;
	
	void AgeDynamicLabels() {
		if ( app_pauseAging.GetVal() ) {
			return;
		}
		map< string, DynamicLabel > oldLabels = dynamicLabels;
		dynamicLabels.clear();
		map< string, DynamicLabel >::iterator it;
		int count = 0;
		float currTime = GetSeconds();
		for ( it = oldLabels.begin(); it != oldLabels.end(); ++it ) {
			DynamicLabel & dl = it->second;
			dl.age();
			if ( dl.state != DynamicLabel::DState_Terminate || ( currTime - dl.lastSeen ) < 1.0f ) {
				dynamicLabels[ dl.name ] = dl;
			}
			count++;
		}
	}
	
	void DrawDynamicLabels( DrawNonOverlappingStrings & nos ) {
		map< string, DynamicLabel >::iterator it;
		for ( it = dynamicLabels.begin(); it != dynamicLabels.end(); ++it ) {
			DynamicLabel & dl = it->second;
			dl.render( nos );
		}
	}
	
	
	struct DynamicLines : public DynamicRenderable {
		DynamicLines() {}
		DynamicLines( Lines *lLines, const Vec3f & lDir, const Vec3f & lLookDir, float lLimit, const Vec4f lColor, float lDuration ) 
		: DynamicRenderable( lDir, lLookDir, lLimit, lColor, lDuration ), lines( lLines ) {
			timeStamp = GetSeconds();
			currAlpha = 0.0f;
		} 
		Lines *lines;
		void render() {
			if ( state != DState_Terminate ) {
				SetColor( Vec4f( color.x, color.y, color.z, currAlpha ) );
				ImVarying( 0 );
				ImBegin( Primitive_Lines );
				for( int j = 0; j < (int)lines->vert.size(); j++ ) {
					Vec3f & v = lines->vert[ j ];
					ImVertex( v.x, v.y, v.z );
				}
				ImEnd();
			}
		}
	};
	
	map< Lines *, DynamicLines > dynamicLines;
	
	void AgeDynamicLines() {
		if ( app_pauseAging.GetVal() ) {
			return;
		}		
		map< Lines *, DynamicLines > oldLines = dynamicLines;
		dynamicLines.clear();
		map< Lines *, DynamicLines >::iterator it;
		int count = 0;
		float currTime = GetSeconds();
		for ( it = oldLines.begin(); it != oldLines.end(); ++it ) {
			DynamicLines & dl = it->second;
			dl.age();
			if ( dl.state != DynamicLabel::DState_Terminate || ( currTime - dl.lastSeen ) < 1.0f ) {
				dynamicLines[ dl.lines ] = dl;
			}
			count++;
		}
	}
	
	void DrawDynamicLines() {
		map< Lines *, DynamicLines >::iterator it;
		for ( it = dynamicLines.begin(); it != dynamicLines.end(); ++it ) {
			DynamicLines & dl = it->second;
			dl.render();
		}
	}
	
	// Draw something to illustrate the horizon
	void DrawUpHemisphere() {
		//Output("DrawUpHemisphere");
		hemiTex->Bind( 0 );
		hemiTex->Enable();
		SetColor( Vec4f( 1, 1, 1, 1 ) );
		hemiModel->Draw();
		hemiTex->Disable();
	}
	
	void DrawEarth() {
		earthTex->Bind( 0 );
		earthTex->Enable();
		SetColor( Vec4f( 1, 1, 1, 1 ) );
		sphereModel->Draw();
		earthTex->Disable();
	}
	
	void GoToAppStore( const vector< Token > & tokens ) {
		OpenURL( "http://phobos.apple.com/WebObjects/MZStore.woa/wa/viewSoftware?id=353613186" );
	}
	CommandFunc GoToAppStoreCmd( "goToAppStore", "go to star3map in the App Store", GoToAppStore );
	
	bool hasViewedGlobe = false;
	float globeViewLat;
	float globeViewLon;
	void SetAppMode( const vector< Token > & tokens ) {
		if ( tokens.size() == 2 ) {
			if ( tokens[1].valString == "viewStars" ) {
				appMode = AppMode_ViewStars;
			} else if ( tokens[1].valString == "viewGlobe" ) {
				appMode = AppMode_ViewGlobe;
				if ( hasViewedGlobe == false ) {
					hasViewedGlobe = true;
					globeViewLat = app_latitude.GetVal();
					globeViewLon = app_longitude.GetVal();
				}
			}
		}
	}
	CommandFunc SetAppModeCmd( "setAppMode", "set app mode :-)", SetAppMode );
	
	void UpdateManualOrientation() {
		Matrix4f phiMat = Rotationf( Vec3f( 1, 0, 0 ), -ToRadians( app_manualPhi.GetVal() + 90 ) ).GetMatrix4();
		Matrix4f thetaMat = Rotationf( Vec3f( 0, 0, 1 ), -ToRadians( app_manualTheta.GetVal() ) ).GetMatrix4();
		//Output( "Updating manualOrientation from phi=%f theta=%f", app_manualPhi.GetVal(), app_manualTheta.GetVal() );
		manualOrientation = phiMat * thetaMat;
	}
	
}




namespace star3map {
	
	bool touchActive = false;
	Vec2f inputInertia;
	void ApplyInputInertia() {
		
		inputInertia *= app_inputDrag.GetVal();
		float len = inputInertia.Length();
		
		if ( touchActive == false && len > 1 ) {
			float dx = inputInertia.x;
			float dy = inputInertia.y;
			if ( appMode == AppMode_ViewGlobe && ( dx != 0 || dy != 0 ) ) {
				globeViewLat = max( -90.f, min( 90.f, globeViewLat - dy ) );
				globeViewLon = ModuloRange( globeViewLon - dx, -180, 180 );
				if ( app_changeLocation.GetVal() ) {
					app_useCoreLocation.SetVal( false );
					app_latitude.SetVal( globeViewLat );
					app_longitude.SetVal( globeViewLon );
				}
			} else if ( appMode == AppMode_ViewStars && app_useCompass.GetVal() == false ) {
				float phi = max( -90.f, min( 90.f, app_manualPhi.GetVal() - dy ) );
				float theta = ModuloRange( app_manualTheta.GetVal() + dx, -180, 180 );
				app_manualPhi.SetVal( phi );
				app_manualTheta.SetVal( theta );
				UpdateManualOrientation();
			}
		}
		
	}

	
	bool ProcessInput( bool active, int x, int y ) {
		
		bool handled = false;
		for ( int i = 0; i < (int)buttons.size(); i++ ) {
			handled = handled || buttons[i]->ProcessInput( active, x, y );
		}
		
		if ( app_useCoreLocation.GetVal() == true && GotLocationUpdate == false ) {
			Output( "setting useCoreLocation back to false because we have not gotten a location update" );
			app_useCoreLocation.SetVal( false );
		}
		if ( app_useCompass.GetVal() == true && GotCompassUpdate == false ) {
			Output( "setting useCompass back to false because we have not gotten a compass update" );
			app_useCompass.SetVal( false );
		}
		if ( app_showSatellites.GetVal() == true && satellite.size() == 0 ) {
			Output( "setting showSatellites to false because we have not gotten TLE data" );
			app_showSatellites.SetVal( false );
		}
		
		static int prevx;
		static int prevy;
		float dx;
		float dy;
		// If the UI didn't handle the input, then try to use it as "global" input
		if ( active && handled == false ) {
			if ( prevx != 0 && prevy != 0 ) {
				float factor = 0.25 * r_fov.GetVal() / 90.0f;
				dx = factor * ( x - prevx ) ;
				dy = factor * ( y - prevy );
				if ( appMode == AppMode_ViewGlobe && ( dx != 0 || dy != 0 ) ) {
					globeViewLat = max( -90.f, min( 90.f, globeViewLat - dy ) );
					globeViewLon = ModuloRange( globeViewLon - dx, -180, 180 );
					if ( app_changeLocation.GetVal() ) {
						app_useCoreLocation.SetVal( false );
						app_latitude.SetVal( globeViewLat );
						app_longitude.SetVal( globeViewLon );
					}
				} else if ( appMode == AppMode_ViewStars && app_useCompass.GetVal() == false ) {
					float phi = max( -90.f, min( 90.f, app_manualPhi.GetVal() - dy ) );
					float theta = ModuloRange( app_manualTheta.GetVal() + dx, -180, 180 );
					app_manualPhi.SetVal( phi );
					app_manualTheta.SetVal( theta );
					UpdateManualOrientation();
				}
			}
			prevx = x;
			prevy = y;
			if ( fabs( float( dx ) ) > fabs( inputInertia.x ) ) {
				inputInertia.x = dx;
			}
			if ( fabs( float( dy ) ) > fabs( inputInertia.y ) ) {
				inputInertia.y = dy;
			}
		}

		if ( active == false ) {
			prevx = prevy = 0;
		}
		
		touchActive = active;
		
		return true;
	}
	
	
	void DisplayViewStars() {
		DrawNonOverlappingStrings nos;
		
		Clear();
		
		PushTransform(); // 0
		ClearTransform();
		r3::Matrix4f proj = r3::Perspective( r_fov.GetVal(), float(r_windowWidth.GetVal()) / r_windowHeight.GetVal(), 0.5f, 100.0f );
		ApplyTransform( proj );
		
		float latitude = ToRadians( app_latitude.GetVal() );
		float longitude = ToRadians( app_longitude.GetVal() );
		Matrix4f xout = Rotationf( Vec3f( 0, 1, 0 ), -R3_PI / 2.0f ).GetMatrix4(); // current Lat/Lon now at { 0, 0, 1 }, with z up
		Matrix4f zup = Rotationf( Vec3f( 1, 0, 0 ), -R3_PI / 2.0f ).GetMatrix4();  // current Lat/Lon now at { 1, 0, 0 }, with z up
		Matrix4f lat = Rotationf( Vec3f( 0, 1, 0 ), latitude ).GetMatrix4();       // current Lat/Lon now at { 1, 0, 0 }, with y up
		Matrix4f lon = Rotationf( Vec3f( 0, 0, 1 ), -longitude ).GetMatrix4();
		float phaseEarthRot = GetCurrentEarthPhase();
		Matrix4f phase = Rotationf( Vec3f( 0, 0, 1 ), -phaseEarthRot ).GetMatrix4();
			
		Matrix4f comp = ( xout * zup * lat * lon * phase );

		
		BlendFunc( BlendFunc_SrcAlpha, BlendFunc_OneMinusSrcAlpha );
		BlendEnable();
		
		SetColor( Vec4f( 1, 1, 1 ) );
		
		// compute culling info
		Matrix4f mvp = proj * orientation * comp;
		Matrix4f imvp = mvp.Inverse();
		Vec3f lookDir = imvp * Vec3f( 0, 0, -1 );
		lookDir.Normalize();
		Vec3f corner = imvp * Vec3f( 1, 1, 1 );
		corner.Normalize();
		float limit = lookDir.Dot( corner );

		float labelLimit = ( limit + 8 ) / 9.0f;
		
		
		PushTransform(); // 1
		ApplyTransform( orientation );			

		// draw horizon hemisphere indicator
		DrawUpHemisphere();	
		
		ApplyTransform( comp );
		
		Matrix3f local = ToMatrix3( comp );
		UpVector = local.GetRow(2); // to orient text correctly

		
		// draw constellations
		for ( int i = 0; i < (int)constellations.size(); i++ ) {
			Lines &l = constellations[ i ];
			Vec4f c( .5, .5, .7, .5 );
			Vec4f cl( .5, .5, .7, .8 );
			if ( lookDir.Dot( l.center ) > l.limit ) {
				if ( dynamicLines.count( &l ) == 0 ) {
					DynamicLines dl( &l, l.center, lookDir, 0, c, 4.0f );
					dynamicLines[ &l ] = dl;
				} else {
					dynamicLines[ &l ].seen();
				}
				if ( dynamicLabels.count( l.name ) == 0 ) {
					DynamicLabel dl( l.name, l.center, lookDir, 0, cl, 2.5f );
					dynamicLabels[ dl.name ] = dl;					
				} else {
					dynamicLabels[ l.name ].seen();
				}
			}
		}
		AgeDynamicLines();
		DrawDynamicLines();

		SetColor( Vec4f( 1, 1, 1, 1 ) );
		
		
		// Reserve space for planet labels.
		Matrix4f axis = Rotationf( Vec3f( 1, 0, 0 ), ToRadians( 23.0 ) ).GetMatrix4();
		Matrix4f iaxis = Rotationf( Vec3f( 1, 0, 0 ), ToRadians( -23.0 ) ).GetMatrix4();
		PushTransform(); // 2
		ApplyTransform( axis );
		{
			UpVector = iaxis * local.GetRow(2);
			// compute culling info
			Matrix4f mvp = proj * orientation * comp * axis;
			Matrix4f imvp = mvp.Inverse();
			Vec3f lookDir = imvp * Vec3f( 0, 0, -1 );
			lookDir.Normalize();
			
			for ( int i = 0; i < (int)solarsystem.size(); i++ ) {
				Sprite & s = solarsystem[i];
				nos.ReserveString( s.name, s.direction, lookDir, limit );
			}
			
		}
		PopTransform();  // 2
		
		UpVector = local.GetRow(2); // to orient text correctly

		nos.DrawString( "Up", local.GetRow(2), lookDir, 0.3f );
		nos.DrawString( "Down", -local.GetRow(2), lookDir, 0.3f );
		SetColor( Vec4f( 1, 1, 1, 1 ) );
		DrawSprite( nTex, 10, local.GetRow(1) );
		DrawSprite( sTex, 10, -local.GetRow(1) );
		DrawSprite( eTex, 10, local.GetRow(0) );
		DrawSprite( wTex, 10, -local.GetRow(0) );

		float dynamicLabelDot = 0.0f;
		string dynamicLabel;
		Vec4f dynamicLabelColor;
		Vec3f dynamicLabelDirection;
		
		static int prev_culled;
		int culled = 0;
		int drew = 0;
		for ( int i = 0; i < (int)stars.size(); i++ ) {
			Sprite & s = stars[i];
			if ( s.magnitude > 4 ) {
				continue;
			}
			float dot = lookDir.Dot( s.direction );
			if ( app_cull.GetVal() && dot < limit ) {
				culled++;
				continue;
			}
			drew++;
			if ( s.name.size() > 0 && dot > labelLimit && dot > dynamicLabelDot && s.magnitude < 2.5 ) {
				float c = ( dot - labelLimit ) / ( 1.0 - labelLimit );
				c = pow( c, 4 );
				dynamicLabelDot = dot;
				dynamicLabel = s.name;
				dynamicLabelColor = Vec4f( 1, 1, 1, c );
				dynamicLabelDirection = s.direction;
			}
		}

		// draw stars
		{
			stars[0].tex->Bind( 0 );
			stars[0].tex->Enable();
			starsModel->Draw();
			stars[0].tex->Disable();
		}
		
		// draw satellites
		if ( app_showSatellites.GetVal() ) {

			double t = GetTime() / 1.0;
			t = t - floor( t );
			float r = t + 0;
			float g = t + 1.0 / 3.0;
			float b = t + 2.0 / 3.0;
			r = ( cos( r * R3_PI * 2.0 ) * .5 + .5 ) * .5 + .5;
			g = ( cos( g * R3_PI * 2.0 ) * .5 + .5 ) * .5 + .5;
			b = ( cos( b * R3_PI * 2.0 ) * .5 + .5 ) * .5 + .5;
			SetColor( Vec4f( r, g, b, 1 ) );
			Matrix4f invPhase = Rotationf( Vec3f( 0, 0, 1 ), phaseEarthRot ).GetMatrix4();
			Vec3f viewer = invPhase * SphericalToCartesian( 6371, latitude, longitude );
			
			for ( int i = 0; i < (int)satellite.size(); i++ ) {
				Vec3f dir = satellite[ i ].pos - viewer;
				dir.Normalize();

				float dot = lookDir.Dot( dir );
				if ( app_cull.GetVal() && dot < limit ) {
					continue;
				}
				
				DrawSprite( satTex, 5, dir );
				
				if ( dot > labelLimit && dot > dynamicLabelDot ) {
					float c = ( dot - labelLimit ) / ( 1.0 - labelLimit );
					c = pow( c, 4 );
					dynamicLabelDot = dot;
					dynamicLabel = satellite[ i ].name;
					dynamicLabelColor = Vec4f( 1, 1, 1, c );
					dynamicLabelDirection = dir;
				}
	
			}
		}
		
		
		AgeDynamicLabels();
		DrawDynamicLabels( nos );
		
		if ( dynamicLabelDot > 0.0f ) {
			if ( dynamicLabels.count( dynamicLabel )  == 0) {
				if ( nos.CanDrawString( dynamicLabel, dynamicLabelDirection, lookDir, limit ) ) {
					DynamicLabel dl( dynamicLabel, dynamicLabelDirection, lookDir, limit, Vec4f( 1, 1, 1, 1), 1.0f );
					dynamicLabels[ dl.name ] = dl;
				}
			} else {
				dynamicLabels[ dynamicLabel ].seen();
			}
		}
		

		if ( culled != prev_culled ) {
			//Output( "Culled %d and drew %d", culled, drew );
			prev_culled = culled;
		}
		
		nos.reserved.clear();
		PushTransform(); // 2
		ApplyTransform( axis );
		{
			UpVector = iaxis * local.GetRow(2);
			// compute culling info
			Matrix4f mvp = proj * orientation * comp * axis;
			Matrix4f imvp = mvp.Inverse();
			Vec3f lookDir = imvp * Vec3f( 0, 0, -1 );
			lookDir.Normalize();
			
			for ( int i = 0; i < (int)solarsystem.size(); i++ ) {
				Sprite & s = solarsystem[i];
				SetColor( s.color );
				DrawSprite( s.tex, s.scale, s.direction );			
				nos.DrawString( s.name, s.direction, lookDir, limit );
			}

		}
		PopTransform(); // 2
		
		BlendDisable();
		PopTransform(); // 1
		
		PopTransform(); // 0
	}
	
	struct IndexedSatDirCompare {
		const vector< Satellite > & sats;
		const Vec3f & dir;
		IndexedSatDirCompare( const vector< Satellite > & satList, const Vec3f & sortDir ) : sats( satList ), dir( sortDir ) {
		}
		bool operator() ( int a, int b ) {
			return dir.Dot( sats[a].pos ) > dir.Dot( sats[b].pos );
		}
	};
	
	
	void DisplayViewGlobe() {
		DrawNonOverlappingStrings nos;

		DepthFunc( Compare_Less );
		DepthTestEnable();
	
		Clear();
		
		PushTransform(); // 0
		ClearTransform();
		r3::Matrix4f proj = r3::Perspective( r_fov.GetVal(),
											float(r_windowWidth.GetVal()) / r_windowHeight.GetVal(), 0.5f, 100.0f );
		ApplyTransform( proj );		
		r3::Matrix4f trans;
		trans.SetTranslate( Vec3f( 0, 0, -2 ) );
		ApplyTransform( trans );

		float latitude = ToRadians( globeViewLat );
		float longitude = ToRadians( globeViewLon );
		Matrix4f xout = Rotationf( Vec3f( 0, 1, 0 ), -R3_PI / 2.0f ).GetMatrix4(); // current Lat/Lon now at { 0, 0, 1 }, with z up
		Matrix4f zup = Rotationf( Vec3f( 1, 0, 0 ), -R3_PI / 2.0f ).GetMatrix4();  // current Lat/Lon now at { 1, 0, 0 }, with z up
		Matrix4f lat = Rotationf( Vec3f( 0, 1, 0 ), latitude ).GetMatrix4();       // current Lat/Lon now at { 1, 0, 0 }, with y up
		Matrix4f lon = Rotationf( Vec3f( 0, 0, 1 ), -longitude ).GetMatrix4();
		float phaseEarthRot = GetCurrentEarthPhase();
		Matrix4f phase = Rotationf( Vec3f( 0, 0, 1 ), -phaseEarthRot ).GetMatrix4();

		Matrix4f comp = ( xout * zup * lat * lon );

		SetColor( Vec4f( 1, 1, 1 ) );

		PushTransform();
		ApplyTransform( comp );		
		DrawEarth();

		PointSmoothEnable();
		BlendEnable();
		AlphaFunc( Compare_GreaterOrEqual, 0.1 );
		AlphaTestEnable();

		// current view position
		{
			Vec3f currPos = SphericalToCartesian( 1.005, ToRadians( app_latitude.GetVal() ), ToRadians( app_longitude.GetVal() ) );
			ImVarying( Varying_PositionBit );
			SetColor( Vec4f( 1, 1, 1, 1 ) );
			PointSize( 3 );
			ImBegin( Primitive_Points );
			ImVertex( currPos.x, currPos.y, currPos.z );
			ImEnd();
		}
		
		if ( app_showSatellites.GetVal() ) {
			PointSize( 3 );
			ApplyTransform( phase );
			
			vector<int> indexes( satellite.size() );
			for ( int i = 0; i < (int)indexes.size(); i++ ) {
				indexes[i] = i;
			}
			Matrix4f invPhase = Rotationf( Vec3f( 0, 0, 1 ), phaseEarthRot ).GetMatrix4();
			Vec3f dir = invPhase * SphericalToCartesian( 1.0, latitude, longitude );

			IndexedSatDirCompare isdc( satellite, dir );
			sort( indexes.begin(), indexes.end(), isdc );
			
			ImVarying( Varying_PositionBit );
			SetColor( Vec4f( 1, 1, 0, 1 ) );
			ImBegin( Primitive_Points );
			
			int num = min( app_maxSatellites.GetVal(), (int)satellite.size() );
			for ( int i = 0; i < num; i++ ) {
				Vec3f p = satellite[ indexes[ i ] ].pos;
				p /= 6371.0f;
				ImVertex( p.x, p.y, p.z );
			}
			ImEnd();

			PushTransform();
			Matrix4f billboard = ToMatrix4( ToMatrix3( GetTransform() ).Inverse() );
			for ( int i = 0; i < num; i++ ) {
				Satellite sat = satellite[ indexes[ i ] ];
				sat.pos /= 6371.0f;
				DrawStringAtLocation( sat.name, sat.pos, billboard );
			}
			PopTransform();
		}

		AlphaTestDisable();
		BlendDisable();
		PointSmoothDisable();

		PopTransform();

		PopTransform();

		DepthTestDisable();
	}
	
	void Display() {
		Initialize();  // do this once instead?
				
		ApplyInputInertia();
		
		if ( GotSatelliteData == false &&
			twoLineElements &&
			twoLineElements->running == false &&
			twoLineElements->urldata.size() > 0 ) {
			// we have satellite data
			GotSatelliteData = true;
			File *f = FileOpenForWrite( "satellite/" + app_satelliteFile.GetVal() );
			if ( f ) {
				f->Write( &twoLineElements->urldata[0], 1, (int)twoLineElements->urldata.size() );
				delete f;
			}
			delete twoLineElements;
			twoLineElements = NULL;
			ReadSatelliteData( "satellite/" + app_satelliteFile.GetVal() );
			//app_showSatellites.SetVal( true );
		}
		
		if ( app_showSatellites.GetVal() ) {
			ComputeSatellitePositions( satellite );
		}
		
		orientation = app_useCompass.GetVal() ? platformOrientation : manualOrientation;
				
		UpdateLatLon();
		
		switch ( appMode ) {
			case AppMode_ViewStars:
				DisplayViewStars();
				break;
			case AppMode_ViewGlobe:
				DisplayViewGlobe();
				break;
			default:
				break;
		}

		// UI
		PlaceButtons();
		
		BlendFunc( BlendFunc_SrcAlpha, BlendFunc_OneMinusSrcAlpha );
		BlendEnable();
		PushTransform(); // 0
		ClearTransform();
		Matrix4f o = Ortho<float>( 0, r_windowWidth.GetVal(), 0, r_windowHeight.GetVal(), -1, 1 );
		ApplyTransform( o );
		for ( int i = 0; i < (int)buttons.size(); i++ ) {
			buttons[i]->Draw();
		}
		PopTransform(); // 0
		BlendDisable();
		
	}		
		
	
	

}


