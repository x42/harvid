harvid -- HTTP Ardour Video Server
==================================

harvid decodes still images from movie files and serves them via HTTP.

Its intended use-case is to efficiently provide frame-accurate data and
act as second level cache for rendering the video-timeline in
[Ardour](http://ardour.org).


Usage
-----

harvid is a stanalone HTTP server, get its build-dependencies, run

    make
		./src/harvid

and point a web-browser at http://localhost:1554/

harvid can be launched as system-service (daemonized, chroot, chuid, syslog),
and listen on specific interfaces only in case you do not want to expose
access to your movie-collection. However, is no per request access control.
See `harvid --help` or read the included man page for details.

Build-dependencies
------------------

[ffmpeg](http://ffmpeg.org/) is used to decode the movie. The source
code should be compatible and compile with [libav](https://libav.org/).

For encoding images,
[libpng](http://www.libpng.org/pub/png/libpng.html)
and [libjpeg](http://libjpeg.sourceforge.net/) are required.


Internals
---------

harvid is highly concurrent makes use of all available CPUs. It will
spawn multiple decoder backends, keep them available for a reasonable
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