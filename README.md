harvid -- HTTP Ardour Video Daemon
==================================

Harvid decodes still images from movie files and serves them via HTTP.

Its intended use-case is to efficiently provide frame-accurate data and
act as second level cache for rendering the video-timeline in
[Ardour](http://ardour.org).


Download
--------

Apart from the source-code and packages from your linux-distributor, binaries
are available for OSX, Windows and Linux at http://x42.github.com/harvid/ .


Usage
-----

Harvid is a standalone HTTP server, all interaction takes place via HTTP.
After launching it, simply point a web-browser at http://localhost:1554/

The OSX bundle and window installer come with a shortcut link to launch
the server. On Linux or with the OSX package, harvid is usually started
from a terminal by simply typing `harvid`&lt;enter&gt;.

Harvid can also be run directly from the source folder without installing
it. Get its build-dependencies (see below), run

	make
	./src/harvid

When used from ardour, ardour will automatically start the server when
you open a video. Ardour searches $PATH or asks your for where it can find
harvid. The easiest way is to simply run:

	sudo make install

Harvid can be launched as system-service (daemonized, chroot, chuid, syslog),
and listen on specific interfaces only in case you do not want to expose
access to your movie-collection. However, is no per request access control.

For available options see `harvid --help` or the included man page which
is also available online at http://x42.github.com/harvid/harvid.1.html


Build-dependencies
------------------

[ffmpeg](http://ffmpeg.org/) is used to decode the movie. The source
code should be compatible and compile with [libav](https://libav.org/).

For encoding images,
[libpng](http://www.libpng.org/pub/png/libpng.html)
and [libjpeg](http://libjpeg.sourceforge.net/) are required.


Internals
---------

Harvid is highly concurrent makes use of all available CPUs. It will
spawn multiple decoder processes, keep them available for a reasonable
time and also cache the video-decoder's output for recurring requests.


The cache-size is variable only limited by available memory.
All images are served from the cache, so even if you are not planning
to use the built-in frame cache, the cache-size defines the minimum
number of concurrent connections.

Interface
---------

The HTTP request interface is documented on the homepage of the server
itself: http://localhost:1554/

The default request-handler will respond to `/?file=PATH&frame=NUMBER`
requests. Optionally `&w=NUM` and `&h=NUM` can be used to alter the geometry
and `&format=FMT` to request specific pixel-formats and/or encodings.

`/index[/PATH]` allows to get a list of available files - either as tree or
as flat-list with the ?flatindex=1 as recursive list of the server's docroot.

`/info?file=PATH` returns information about the video-file.

Furthermore there are built-in request handlers for status-information,
server-version and configuration as well as admin-tasks such as flushing
the cache or closing decoders.

The `&format=FMT` also applies for information requests with
HTML, JSON, CSV and plain text as available formatting options.
