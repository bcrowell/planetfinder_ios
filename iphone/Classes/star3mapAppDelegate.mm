//
//  star3mapAppDelegate.m
//  star3map
//
//  Created by Cass Everitt on 1/30/10.
//  Copyright __MyCompanyName__ 2010. All rights reserved.
//

#import "star3mapAppDelegate.h"
#import "EAGLView.h"

#include <string>
using namespace std;

#include "app.h"

@implementation star3mapAppDelegate

@synthesize window;
@synthesize glView;
@synthesize locationManager;


- (void) applicationDidFinishLaunching:(UIApplication *)application
{	
	platformMain();
	[glView startAnimation];
	// Create the manager object 
    self.locationManager = [[[CLLocationManager alloc] init] autorelease];
    locationManager.delegate = self;
	[locationManager startUpdatingLocation];
	[locationManager startUpdatingHeading];

	// start the flow of accelerometer events
	UIAccelerometer *accelerometer = [UIAccelerometer sharedAccelerometer];
	accelerometer.delegate = self;
	accelerometer.updateInterval = 0.1;
}

- (void) applicationWillResignActive:(UIApplication *)application
{
	[glView stopAnimation];
}

- (void) applicationDidBecomeActive:(UIApplication *)application
{
	[glView startAnimation];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
	[glView stopAnimation];
	platformQuit();
}

- (void) dealloc
{
	[window release];
	[glView release];
	
	[super dealloc];
}


- (void)locationManager:(CLLocationManager *)manager didUpdateToLocation:(CLLocation *)newLocation fromLocation:(CLLocation *)oldLocation {
    if (newLocation.horizontalAccuracy < 0) return;
    NSTimeInterval locationAge = -[newLocation.timestamp timeIntervalSinceNow];
    if (locationAge > 5.0) return;
	
	float lat = newLocation.coordinate.latitude;
	float lon = newLocation.coordinate.longitude;
	setLocation( lat, lon );
	
}

- (void)locationManager:(CLLocationManager *)manager didUpdateHeading:(CLHeading *)newHeading {
	setHeading( newHeading.x, newHeading.y, newHeading.z, newHeading.trueHeading - newHeading.magneticHeading );
}


- (void)accelerometer:(UIAccelerometer *)accelerometer didAccelerate:(UIAcceleration *)acceleration
{
	setAccel( acceleration.x, acceleration.y, acceleration.z );
}



@end


