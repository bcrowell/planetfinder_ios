planetfinder_ios
================

This is an app for the iOS platform that shows the locations of the planets, stars, moon, and
sun in the sky from any location and for any date and time. It is descended from the java
applet [Planet Finder](http://www.lightandmatter.com/planetfinder/en/), and was translated
into C++ and ported to mobile devices by 
[Kostas Giannakakis](http://users.forthnet.gr/ath/kgiannak/)
and [Cass Everitt](http://www.xyzw.us/star3map/sourcecode.html). Giannakakis's
Symbian app no longer appears to be available from his web site. Everitt's iPhone app was
originally open source, but he later eliminated all GPL-licensed code from it and made it
closed source. Although the source code for the older, open-source app is still available
from Everitt's web site, I decided to preserve the code here under the name
planetfinder_ios, since it seemed likely that it would eventually disappear from the web.

I have not tried to build the program, because I don't own a Mac or
an iPhone. I've moved a few directories around, which would presumably break the
build.

Everitt added a satellite-tracking feature, which was based on a library called
SGP4 by David A. Vallado. The library appears to consist of
code distributed along with Vallado's
book Fundamentals of Astrodynamics and Applications, and is currently available
[here](http://www.celestrak.com/software/vallado-sw.asp).
Due to an unclear licensing situation,
the SGP4 source code is not distributed as part of this project. Anyone interested
in building the project should either consult with Dr. Vallado to clarify the
licensing or modify the code to eliminate the satellite tracking
features that depend on it.

If anyone is interested in getting this to build, I suggest that they simply fork the
github project and go ahead.
