/*
 *  app.h
 *  star3map
 *
 *  Created by Cass Everitt on 1/31/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef __APP_H__
#define __APP_H__

// shameful lack of consistency in naming... Cass

void SwapBuffers();
void reshape( int width, int height );
void setLocation( float latitude, float longitude );
void setHeading( float x, float y, float z, float trueDiff );
void setAccel( float x, float y, float z );
void touches( int count, int *points );
void display();
void platformMain();
void platformQuit();



#endif // __APP_H__