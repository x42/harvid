/*
   harvid -- http ardour video daemon

   Copyright (C) 2008-2014 Robin Gareus <robin@gareus.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include <sys/stat.h>
#include <libgen.h> // basename
#include <locale.h>

#include "daemon_log.h"
#include "daemon_util.h"
#include "socket_server.h"

#include <harvid.h>
#include "image_format.h"
#include "enums.h"

#include "ffcompat.h"

#ifndef HAVE_WINDOWS
#include <arpa/inet.h> // inet_addr
#include <sys/mman.h>  // memlock
#endif

#ifndef DEFAULT_PORT
#define DEFAULT_PORT 1554
#endif

char *str_escape(const char *string, int inlength, const char esc); //  defined in fileindex.c

extern int debug_level;
extern int debug_section;

char *program_name;
int   want_quiet = 0;
int   want_verbose = 0;
int   cfg_daemonize = 0;
int   cfg_syslog = 0;
int   cfg_memlock = 0;
int   cfg_timeout = 0;
int   cfg_usermask = USR_INDEX;
int   cfg_adminmask = ADM_FLUSHCACHE;
char *cfg_logfile = NULL;
char *cfg_chroot = NULL;
char *cfg_username = NULL;
char *cfg_groupname = NULL;
int   initial_cache_size = 128;
int   max_decoder_threads = 8;
unsigned short  cfg_port = DEFAULT_PORT;
unsigned int    cfg_host = 0; /* = htonl(INADDR_ANY) */

static void printversion (void) {
  printf ("harvid %s\n", ICSVERSION);
  printf ("Compiled with %s %s %s\n\n", LIBAVFORMAT_IDENT, LIBAVCODEC_IDENT, LIBAVUTIL_IDENT);
  printf ("Copyright (C) GPL 2002-2013 Robin Gareus <robin@gareus.org>\n");
}

static void usage (int status) {
  printf ("%s - http ardour video server\n\n", basename(program_name));
  printf ("Usage: %s [OPTION] [document-root]\n", program_name);
  printf ("\n"
"Options:\n"
"  -A <cmdlist>, --admin <cmdlist>\n"
"                             space separated list of allowed admin commands.\n"
"                             An exclamation-mark before a command disables it.\n"
"                             default: 'flush_cache';\n"
"                             available: flush_cache, purge_cache, shutdown\n"
"  -c <path>, --chroot <path>\n"
"                             change system root - jails server to this path\n"
"  -C <frames>                set initial frame-cache size (default: 128)\n"
"  -D, --daemonize            fork into background and detach from TTY\n"
"  -g <name>, --groupname <name>\n"
"                             assume this user-group\n"
"  -h, --help                 display this help and exit\n"
"  -F <feat>, --features <feat>\n"
"                             space separated list of optional features.\n"
"                             An exclamation-mark before a features disables it.\n"
"                             default: 'index';\n"
"                             available: index, seek, flatindex, keepraw\n"
"  -l <path>, --logfile <path>\n"
"                             specify file for log messages\n"
"  -M, --memlock              attempt to lock memory (prevent cache paging)\n"
"  -p <num>, --port <num>     TCP port to listen on (default %i)\n"
"  -P <listenaddr>            IP address to listen on (default 0.0.0.0)\n"
"  -q, --quiet, --silent      inhibit usual output (may be used thrice)\n"
"  -s, --syslog               send messages to syslog\n"
"  -t <thread-limit>          set maximum decoder-threads (default: 8)\n"
"  -T <sec>, --timeout <secs>\n"
"                             set a timeout after which the server will\n"
"                             terminate if no new request arrives\n"
"  -u <name>, --username <name>\n"
"                             server will act as this user\n"
"  -v, --verbose              print more information (may be used twice)\n"
"  -V, --version              print version information and exit\n"
"\n"
"The default document-root (if unspecified) is the system root: / or C:\\.\n"
"\n"
"If both syslog and logfile are given that last specified option will be used.\n"
"\n"
"--verbose and --quiet are additive. The default is to print warnings\n"
"and above only. Available log-levels are 'mute', 'critical, 'error',\n"
"'warning' and 'info'.\n"
"\n"
"The --features option allows one to enable or disable /seek and /index\n"
"http-handlers and change their respone: The 'flatindex' option concerns\n"
"/index&flatindex=1 which recursively indexes all video file. It is disabled\n"
"by defaults since a recursive search of the default docroot / can takes a\n"
"very long time.\n"
"When requesting png or jpeg images harvid decodes a raw RGB frame and then\n"
"encodes it again. If 'keepraw' feature is enabled, both the raw RGB and\n"
"encoded image are kept in cache. The default is to invaldate the RGB frame\n"
"after encoding the image.\n"
"\n"
"Examples:\n"
"harvid -A '!flush_cache purge_cache shutdown' -C 256 /tmp/\n"
"\n"
"sudo harvid -c /tmp/ -u nobody -g nogroup /\n"
"\n"
"Report bugs to <robin@gareus.org> or https://github.com/x42/harvid/issues\n"
"Website http://x42.github.com/harvid/\n"
, DEFAULT_PORT
);
  exit (status);
}

static struct option const long_options[] =
{
  {"admin", required_argument, 0, 'A'},
  {"chroot", required_argument, 0, 'c'},
  {"cache-size", required_argument, 0, 'C'},
  {"debug", required_argument, 0, 'd'},
  {"daemonize", no_argument, 0, 'D'},
  {"groupname", required_argument, 0, 'g'},
  {"help", no_argument, 0, 'h'},
  {"features", required_argument, 0, 'F'},
  {"logfile", required_argument, 0, 'l'},
  {"memlock", no_argument, 0, 'M'},
  {"port", required_argument, 0, 'p'},
  {"listenip", required_argument, 0, 'P'},
  {"quiet", no_argument, 0, 'q'},
  {"silent", no_argument, 0, 'q'},
  {"syslog", no_argument, 0, 's'},
  {"timeout", required_argument, 0, 'T'},
  {"username", required_argument, 0, 'u'},
  {"verbose", no_argument, 0, 'v'},
  {"version", no_argument, 0, 'V'},
  {NULL, 0, NULL, 0}
};


/* Set all the option flags according to the switches specified.
   Return the index of the first non-option argument.  */
static int decode_switches (int argc, char **argv) {
  int c;
  while ((c = getopt_long (argc, argv,
         "A:"	/* admin */
         "c:"	/* chroot-dir */
         "C:" 	/* initial cache size */
         "d:"	/* debug */
         "D"	/* daemonize */
         "g:"	/* setGroup */
         "h"	/* help */
         "F:"	/* interaction */
         "l:"	/* logfile */
         "M"	/* memlock */
         "p:"	/* port */
         "P:"	/* IP */
         "q"	/* quiet or silent */
         "s"	/* syslog */
         "t:"	/* threads */
         "T:"	/* timeout */
         "u:"	/* setUser */
         "v"	/* verbose */
         "V",	/* version */
         long_options, (int *) 0)) != EOF)
  {
    switch (c) {
      case 'q':		/* --quiet, --silent */
        want_quiet = 1;
        want_verbose = 0;
        if (debug_level == DLOG_CRIT)
          debug_level = DLOG_EMERG;
        else if (debug_level == DLOG_ERR)
          debug_level = DLOG_CRIT;
        else
          debug_level=DLOG_ERR;
        break;
      case 'v':		/* --verbose */
        if (debug_level == DLOG_INFO) want_verbose = 1;
        debug_level=DLOG_INFO;
        break;

      case 'A':		/* --admin */
        if (strstr(optarg, "shutdown")) cfg_adminmask|=ADM_SHUTDOWN;
        if (strstr(optarg, "purge_cache")) cfg_adminmask|=ADM_PURGECACHE;
        if (strstr(optarg, "flush_cache")) cfg_adminmask|=ADM_FLUSHCACHE;
        if (strstr(optarg, "!shutdown")) cfg_adminmask&=~ADM_SHUTDOWN;
        if (strstr(optarg, "!purge_cache")) cfg_adminmask&=~ADM_PURGECACHE;
        if (strstr(optarg, "!flush_cache")) cfg_adminmask&=~ADM_FLUSHCACHE;
        break;
      case 'c':		/* --chroot */
        cfg_chroot = optarg;
        break;
      case 'C':
        initial_cache_size = atoi(optarg);
        if (initial_cache_size < 2 || initial_cache_size > 65535)
          initial_cache_size = 128;
        break;
      case 'd':		/* --debug */
        if (strstr(optarg, "SRV")) debug_section|=DEBUG_SRV;
        if (strstr(optarg, "HTTP")) debug_section|=DEBUG_HTTP;
        if (strstr(optarg, "CON")) debug_section|=DEBUG_CON;
        if (strstr(optarg, "DCTL")) debug_section|=DEBUG_DCTL;
        if (strstr(optarg, "ICS")) debug_section|=DEBUG_ICS;
#ifdef NDEBUG
        fprintf(stderr, "harvid was built with NDEBUG. '-d' has no affect.\n");
#endif
        break;
      case 'D':		/* --daemonize */
        cfg_daemonize = 1;
        break;
      case 'F':		/* --features */
        if (strstr(optarg, "index"))      cfg_usermask |=  USR_INDEX;
        if (strstr(optarg, "seek"))       cfg_usermask |=  USR_WEBSEEK;
        if (strstr(optarg, "flatindex"))  cfg_usermask |=  USR_FLATINDEX;
        if (strstr(optarg, "keepraw"))    cfg_usermask |=  USR_KEEPRAW;
        if (strstr(optarg, "!index"))     cfg_usermask &= ~USR_INDEX;
        if (strstr(optarg, "!seek"))      cfg_usermask |=  USR_WEBSEEK;
        if (strstr(optarg, "!flatindex")) cfg_usermask &= ~USR_FLATINDEX;
        if (strstr(optarg, "!keepraw"))   cfg_usermask &= ~USR_KEEPRAW;
        break;
      case 'g':		/* --group */
        cfg_groupname = optarg;
        break;
      case 'l':		/* --logfile */
        cfg_syslog = 0;
        if (cfg_logfile) free(cfg_logfile);
        cfg_logfile = strdup(optarg);
        break;
      case 'M':		/* --memlock */
        cfg_memlock = 1;
        break;
      case 'P':		/* --listenip */
        cfg_host = inet_addr (optarg);
        break;
      case 'p':		/* --port */
        {int pn = atoi(optarg);
        if (pn > 0 && pn < 65536)
          cfg_port = (unsigned short) atoi(optarg);
        }
        break;
      case 's':		/* --syslog */
        cfg_syslog = 1;
        if (cfg_logfile) free(cfg_logfile);
        cfg_logfile = NULL;
        break;
      case 't':
        max_decoder_threads = atoi(optarg);
        if (max_decoder_threads < 2 || max_decoder_threads > 128)
          max_decoder_threads = 8;
        break;
      case 'T':		/* --timeout */
        cfg_timeout = atoi(optarg);
        break;
      case 'u':		/* --username */
        cfg_username = optarg;
        break;
      case 'V':
        printversion();
        exit(0);
      case 'h':
        usage (0);
      default:
        usage (1);
    }
  }
  return optind;
}

// -=-=-=-=-=-=- main -=-=-=-=-=-=-
#include "ffcompat.h"

void *dc = NULL; // decoder control
void *vc = NULL; // video frame cache
void *ic = NULL; // encoded image cache

int main (int argc, char **argv) {
  program_name = argv[0];
  struct stat sb;
  uid_t cfg_uid;
  gid_t cfg_gid;
#ifdef HAVE_WINDOWS
  char *docroot = "C:\\";
#else
  char *docroot = "/";
#endif
  debug_level = DLOG_WARNING;
  int exitstatus = 0;

  // TODO read rc file

  int i = decode_switches (argc, argv);

  if ((i+1)== argc) docroot = argv[i];
  else if (docroot && i==argc) ; // use default
  else usage(1);

  // TODO read additional rc file (from options)

  if (cfg_daemonize && !cfg_logfile && !cfg_syslog) {
    dlog(DLOG_WARNING, "daemonizing without log file or syslog.\n");
  }

  if (cfg_daemonize || cfg_syslog || cfg_logfile) {
    /* ffdecoder prints to stdout */
    want_verbose = 0;
    want_quiet = 1;
  }

  /* initialize */

  if (cfg_logfile || cfg_syslog) dlog_open(cfg_logfile);

  /* resolve before doing chroot() */
  cfg_uid = resolve_uid(cfg_username);
  cfg_gid = resolve_gid(cfg_groupname);

  if (cfg_chroot) {
    if (do_chroot(cfg_chroot)) {exitstatus = -1; goto errexit;}
  }

#ifdef HAVE_WINDOWS
  if (strlen (docroot) == 0) {
    dlog(DLOG_INFO, "No document-root, allowing all drive-letters\n");
  } else
#endif
  if (stat(docroot, &sb) || !S_ISDIR(sb.st_mode)) {
    dlog(DLOG_CRIT, "document-root is not a directory\n");
    exitstatus = -1;
    goto errexit;
  }

  if (cfg_daemonize) {
    if (daemonize()) {exitstatus = -1; goto errexit;}
  }

  ff_initialize();

  vcache_create(&vc);
  vcache_resize(&vc, initial_cache_size);
  icache_create(&ic);
  icache_resize(ic, initial_cache_size*4);
  dctrl_create(&dc, max_decoder_threads, initial_cache_size);

  if (cfg_memlock) {
#ifndef HAVE_WINDOWS
    if (mlockall(MCL_CURRENT|MCL_FUTURE)) {
      dlog(LOG_WARNING, "failed to lock memory.\n");
    }
#else
    dlog(LOG_WARNING, "memory locking is not available on windows.\n");
#endif
  }

  char const* const current_c_locale = setlocale (LC_NUMERIC, 0);
  if (strcmp ("C", current_c_locale) != 0) {
    dlog(DLOG_INFO, "Setting locate to 'C' for portable numerics.\n");
    setlocale (LC_NUMERIC, "C");
  }

  /* all systems go */

  dlog(DLOG_INFO, "Initialization complete. Starting server.\n");
  exitstatus = start_tcp_server(cfg_host, cfg_port, docroot, cfg_uid, cfg_gid, cfg_timeout, NULL);

  /* cleanup */

  ff_cleanup();
  dctrl_destroy(&dc);
  vcache_destroy(&vc);
  icache_destroy(&ic);
errexit:
  dlog_close();
  return(exitstatus);
}

//  -=-=-=-=-=-=- video server callbacks -=-=-=-=-=-=-
/*
 * these are called from protocol_handler() in httprotocol.c
 */
#include "httprotocol.h"
#include "ics_handler.h"
#include "htmlconst.h"

#define HPSIZE 4096 // max size of homepage in bytes.
char *hdl_homepage_html (CONN *c) {
  char *msg = malloc(HPSIZE * sizeof(char));
  int off = 0;
  off+=snprintf(msg+off, HPSIZE-off, DOCTYPE HTMLOPEN);
  off+=snprintf(msg+off, HPSIZE-off, "<title>harvid</title></head>\n");
  off+=snprintf(msg+off, HPSIZE-off, HTMLBODY);
  off+=snprintf(msg+off, HPSIZE-off, CENTERDIV);
  off+=snprintf(msg+off, HPSIZE-off, "<div style=\"text-align:center;\"><h1>Harvid - HTTP Ardour Video Daemon</h1></div>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<div style=\"float:left;margin:0 2em;\"><h2>Built-in handlers</h2>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<ul>");
  if (cfg_usermask & USR_INDEX) {
    off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"index/\">File Index</a></li>\n");
  }
  off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"status/\">Server Status</a></li>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"rc/\">Server Config</a></li>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"version/\">Server Version</a></li>\n");
  off+=snprintf(msg+off, HPSIZE-off, "</ul></div>");

  if (cfg_adminmask)
    off+=snprintf(msg+off, HPSIZE-off, "<div style=\"float:right;margin:0 2em;\"><h2>Admin Tasks:</h2><ul>\n");
  if (cfg_adminmask&ADM_FLUSHCACHE)
    off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"admin/flush_cache\">Flush Cache</a></li>\n");
  if (cfg_adminmask&ADM_PURGECACHE)
    off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"admin/purge_cache\">Purge Cache</a></li>\n");
  if (cfg_adminmask&ADM_SHUTDOWN)
    off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"admin/shutdown\">Server Shutdown</a></li>\n");
  if (cfg_adminmask)
    off+=snprintf(msg+off, HPSIZE-off, "</ul>\n</div>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<div style=\"clear:both;\"></div><hr/>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p style=\"text-align:justify;\">The default request handler decodes images and requires a <code>?frame=NUM&amp;file=PATH</code> URL query or post parameters. Video frames are counted starting at zero. Default options are <code>w=0&amp;h=0&amp;format=png</code> which serves the image pre-scaled to its effective size as png.</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p>The <code>/info</code> request handler requires a <code>?file=PATH</code> query parameter and optionally takes a <code>format</code> (default is html). All other handlers (/status, /rc, /version, /admin/) take no arguments.</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p>Available query parameters: <code>frame</code>, <code>w</code>, <code>h</code>, <code>file</code>, <code>format</code>.</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p>Frame (frame-number), w (width) and h (height) are unsigned integers.</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p>Supported image output pixel formats:</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<ul>\n<li><em>Encoded</em>: jpg, jpeg, png, ppm</li>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<li><em>Raw RGB</em>: rgb, bgr, rgba, argb, bgra</li>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<li><em>Raw YUV</em>: yuv, yuv420, yuv440, yuv422, uyv422</li>\n</ul>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p>Available info output formats:</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<ul>\n<li><em>Human Readable</em>: html, xhtml</li>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<li><em>Machine Readable</em>: json, csv, plain</li>\n</ul>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p style=\"text-align:justify;\">The jpg (and jpeg) <em>format</em> parameter can be postfixed number to specify the jpeg quality. e.g. <code>&format=jpeg90</code>. The default is 75. Note that 'jpg' is just an alias for 'jpeg', and 'html' is an alias for 'xhtml'.</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p style=\"text-align:justify;\">If either only <em>width</em> or <em>height</em> is specified with a value greater than 15, the other is calculated according to the movie's effective aspect-ratio. However the minimum size is 16x16, requesting geometries smaller than 16x16 will return the image in its original size.</p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "<p style=\"text-align:center\"><a href=\"http://x42.github.com/harvid/\">harvid @ GitHub</a></p>\n");
  off+=snprintf(msg+off, HPSIZE-off, "</div>\n");
  off+=snprintf(msg+off, HPSIZE-off, HTMLFOOTER, c->d->local_addr, c->d->local_port);
  off+=snprintf(msg+off, HPSIZE-off, "\n</body>\n</html>");
  return msg;
}

char *hdl_server_status_html (CONN *c) {
  size_t ss = 1024;
  size_t off = 0;
  char *sm = malloc(ss * sizeof(char));
  raprintf(sm, off, ss, DOCTYPE HTMLOPEN);
  raprintf(sm, off, ss, "<title>harvid status</title>\n");
  raprintf(sm, off, ss, "<style type=\"text/css\">\ntd.left{text-align:left;} td.line{border-bottom: 1px solid black;} td.dline{border-top: 3px double black;} td h3 {margin: 1em 0 0 0;}\n</style>\n");
  raprintf(sm, off, ss, "</head>\n");
  raprintf(sm, off, ss, HTMLBODY);
  raprintf(sm, off, ss, "<h2>harvid status</h2>\n");
  raprintf(sm, off, ss, "<!--status: ok, online.-->\n"); // ardour3 reads this
  raprintf(sm, off, ss, "<p>Concurrent connections: (current / max-seen / limit) %d / %d / %d</p>\n", c->d->num_clients, c->d->max_clients, MAXCONNECTIONS);
#ifdef USAGE_FREQUENCY_STATISTICS
  time_t i;
  const time_t n = time(NULL);
  double avg1, avg5, avgA;
  avg1 = avg5 = avgA = 0.0;
  for (i = 0; i < FREQ_LEN; ++i) { avgA += c->d->req_stats[i]; }
  for (i = 0; i < 60; ++i)       { avg1 += c->d->req_stats[(n-i)%FREQ_LEN]; }
  for (i = 0; i < 300; ++i)      { avg5 += c->d->req_stats[(n-i)%FREQ_LEN]; }
  avgA /= (double) FREQ_LEN;
  avg1 /= 60.0;
  avg5 /= 300.0;
  long int uptime = (long int) (n - c->d->stat_start);
  raprintf(sm, off, ss, "<p>Requests/sec: (1min avg / 5min avg / 1h avg / all time) %.2f / %.2f / %.2f / %.3f</p>\n",
      avg1, avg5, avgA, c->d->stat_count / difftime(n, c->d->stat_start));
  raprintf(sm, off, ss, "<p>Total requests: %d, uptime: %ld day%s, %02ld:%02ld:%02ld</p>\n",
      c->d->stat_count, uptime / 86400, (uptime / 86400) == 1 ? "": "s", (uptime % 86400) / 3600, (uptime % 3600) / 60, uptime %60);
#endif
  dctrl_info_html(dc, &sm, &off, &ss, 2);
  vcache_info_html(vc, &sm, &off, &ss, 0);
  icache_info_html(ic, &sm, &off, &ss, 2);
  raprintf(sm, off, ss, HTMLFOOTER, c->d->local_addr, c->d->local_port);
  raprintf(sm, off, ss, "</body>\n</html>");
  return (sm);
}

#define NFOSIZ (256)
static char *file_info_json (CONN *c, ics_request_args *a, VInfo *ji) {
  char *im = malloc(NFOSIZ * sizeof(char));
  int off = 0;
  off+=snprintf(im+off, NFOSIZ-off, "{");
//off+=snprintf(im+off, NFOSIZ-off, "\"geometry\":[%i,%i],", ji->movie_width, ji->movie_height);
  off+=snprintf(im+off, NFOSIZ-off, "\"width\":%i", ji->movie_width);
  off+=snprintf(im+off, NFOSIZ-off, ",\"height\":%i", ji->movie_height);
  off+=snprintf(im+off, NFOSIZ-off, ",\"aspect\":%.3f", ji->movie_aspect);
  off+=snprintf(im+off, NFOSIZ-off, ",\"framerate\":%.3f", timecode_rate_to_double(&ji->framerate));
  off+=snprintf(im+off, NFOSIZ-off, ",\"duration\":%"PRId64, ji->frames);
  off+=snprintf(im+off, NFOSIZ-off, "}");
  jvi_free(ji);
  return (im);
}

static char *file_info_csv (CONN *c, ics_request_args *a, VInfo *ji) {
  char *im = malloc(NFOSIZ * sizeof(char));
  int off = 0;
  off+=snprintf(im+off, NFOSIZ-off, "1"); // FORMAT VERSION
  off+=snprintf(im+off, NFOSIZ-off, ",%i", ji->movie_width);
  off+=snprintf(im+off, NFOSIZ-off, ",%i", ji->movie_height);
  off+=snprintf(im+off, NFOSIZ-off, ",%f", ji->movie_aspect);
  off+=snprintf(im+off, NFOSIZ-off, ",%.3f", timecode_rate_to_double(&ji->framerate));
  off+=snprintf(im+off, NFOSIZ-off, ",%"PRId64, ji->frames);
  off+=snprintf(im+off, NFOSIZ-off, "\n");
  jvi_free(ji);
  return (im);
}

#define FIHSIZ 8192
static char *file_info_html (CONN *c, ics_request_args *a, VInfo *ji) {
  char *im = malloc(FIHSIZ * sizeof(char));
  int off = 0;
  char smpte[14], *tmp;
  timecode_framenumber_to_string(smpte, &ji->framerate, ji->frames);

  off+=snprintf(im+off, FIHSIZ-off, DOCTYPE HTMLOPEN);
  off+=snprintf(im+off, FIHSIZ-off, "<title>harvid file info</title></head>\n");
  off+=snprintf(im+off, FIHSIZ-off, HTMLBODY);
  off+=snprintf(im+off, FIHSIZ-off, CENTERDIV);
  off+=snprintf(im+off, FIHSIZ-off, "<h2>File info</h2>\n\n");
  tmp = url_escape(a->file_qurl, 0);
  if (cfg_usermask & USR_WEBSEEK) {
    off+=snprintf(im+off, FIHSIZ-off, "<p>File: <a href=\"/seek?frame=0&amp;file=%s\">%s</a></p><ul>\n", tmp, a->file_qurl); free(tmp);
  } else {
    off+=snprintf(im+off, FIHSIZ-off, "<p>File: <a href=\"/?frame=0&amp;file=%s\">%s</a></p><ul>\n", tmp, a->file_qurl); free(tmp);
  }
  off+=snprintf(im+off, FIHSIZ-off, "<li>Geometry: %ix%i</li>\n", ji->movie_width, ji->movie_height);
  off+=snprintf(im+off, FIHSIZ-off, "<li>Aspect-Ratio: %.3f</li>\n", ji->movie_aspect);
  off+=snprintf(im+off, FIHSIZ-off, "<li>Framerate: %.2f</li>\n", timecode_rate_to_double(&ji->framerate));
  off+=snprintf(im+off, FIHSIZ-off, "<li>Duration: %s</li>\n", smpte);
  off+=snprintf(im+off, FIHSIZ-off, "<li>Duration: %.2f sec</li>\n", (double)ji->frames/timecode_rate_to_double(&ji->framerate));
  off+=snprintf(im+off, FIHSIZ-off, "<li>Duration: %"PRId64" frames</li>\n", ji->frames);
  off+=snprintf(im+off, FIHSIZ-off, "</ul>\n</div>\n");
  off+=snprintf(im+off, FIHSIZ-off, HTMLFOOTER, c->d->local_addr, c->d->local_port);
  off+=snprintf(im+off, FIHSIZ-off, "</body>\n</html>");
  jvi_free(ji);
  return (im);
}

static char *file_info_raw (CONN *c, ics_request_args *a, VInfo *ji) {
  char *im = malloc(NFOSIZ * sizeof(char));
  int off = 0;
  char smpte[14];
  timecode_framenumber_to_string(smpte, &ji->framerate, ji->frames);

  off+=snprintf(im+off, NFOSIZ-off, "1\n"); // FORMAT VERSION
  off+=snprintf(im+off, NFOSIZ-off, "%.3f\n", timecode_rate_to_double(&ji->framerate)); // fps
  off+=snprintf(im+off, NFOSIZ-off, "%"PRId64"\n", ji->frames); // duration
  off+=snprintf(im+off, NFOSIZ-off, "0.0\n"); // start-offset TODO
  off+=snprintf(im+off, NFOSIZ-off, "%f\n", ji->movie_aspect);
  jvi_free(ji);
  return (im);
}

char *hdl_file_info (CONN *c, ics_request_args *a) {
  VInfo ji;
  unsigned short vid;
  int err = 0;
  vid = dctrl_get_id(vc, dc, a->file_name);
  jvi_init(&ji);
  if ((err=dctrl_get_info(dc, vid, &ji))) {
    if (err == 503) {
      httperror(c->fd, 503, "Service Temporarily Unavailable", "<p>No decoder is available. The server is currently busy or overloaded.</p>");
    } else {
      httperror(c->fd, 500, "Service Unavailable", "<p>No decoder is available: File is invalid (no video track, unknown codec, invalid geometry,..)</p>");
    }
    return NULL;
  }
  switch (a->render_fmt) {
    case OUT_PLAIN:
      return file_info_raw(c, a, &ji);
    case OUT_JSON:
      return file_info_json(c, a, &ji);
    case OUT_CSV:
      return file_info_csv(c, a, &ji);
    default:
      return file_info_html(c, a, &ji);
  }
}

/////////////

#define SINFOSIZ 2048
char *hdl_server_info (CONN *c, ics_request_args *a) {
  char *info = malloc(SINFOSIZ * sizeof(char));
  int off = 0;
  switch (a->render_fmt) {
    case OUT_PLAIN:
      off+=snprintf(info+off, SINFOSIZ-off, "%s\n", c->d->docroot);
      off+=snprintf(info+off, SINFOSIZ-off, "%s\n", c->d->local_addr);
      off+=snprintf(info+off, SINFOSIZ-off, "%d\n", c->d->local_port);
      off+=snprintf(info+off, SINFOSIZ-off, "%d\n", initial_cache_size);
      break;
    case OUT_JSON:
      {
      char *tmp;
      off+=snprintf(info+off, SINFOSIZ-off, "{");
      tmp = str_escape(c->d->docroot, 0, '\\');
      off+=snprintf(info+off, SINFOSIZ-off, "\"docroot\":\"%s\"", tmp); free(tmp);
      off+=snprintf(info+off, SINFOSIZ-off, ",\"listenaddr\":\"%s\"", c->d->local_addr);
      off+=snprintf(info+off, SINFOSIZ-off, ",\"listenport\":%d", c->d->local_port);
      off+=snprintf(info+off, SINFOSIZ-off, ",\"cachesize\":%d", initial_cache_size);
      off+=snprintf(info+off, SINFOSIZ-off, ",\"infohandlers\":[\"/info\", \"/rc\", \"/status\", \"/version\"%s\"",
          cfg_usermask & USR_INDEX ? ",\"index\"":"");
      off+=snprintf(info+off, SINFOSIZ-off, ",\"admintasks\":[\"/check\"%s%s%s]",
          (cfg_adminmask & ADM_FLUSHCACHE) ? ",\"/flush_cache\"" : "",
          (cfg_adminmask & ADM_PURGECACHE) ? ",\"/purge_cache\"" : "",
          (cfg_adminmask & ADM_SHUTDOWN)   ? ",\"/shutdown\"" : ""
          );
      off+=snprintf(info+off, SINFOSIZ-off, "}");
      }
      break;
    case OUT_CSV:
      {
      char *tmp = str_escape(c->d->docroot, 0, '"');
      off+=snprintf(info+off, SINFOSIZ-off, "\"%s\"", tmp); free(tmp);
      off+=snprintf(info+off, SINFOSIZ-off, ",%s", c->d->local_addr);
      off+=snprintf(info+off, SINFOSIZ-off, ",%d", c->d->local_port);
      off+=snprintf(info+off, SINFOSIZ-off, ",%d", initial_cache_size);
      off+=snprintf(info+off, SINFOSIZ-off, ",\"/info /rc /status /version%s\"",
          cfg_usermask & USR_INDEX ? " index":"");
      off+=snprintf(info+off, SINFOSIZ-off, ",\"/check%s%s%s\"",
          (cfg_adminmask & ADM_FLUSHCACHE) ? " /flush_cache" : "",
          (cfg_adminmask & ADM_PURGECACHE) ? " /purge_cache" : "",
          (cfg_adminmask & ADM_SHUTDOWN)   ? " /shutdown" : ""
          );
      off+=snprintf(info+off, SINFOSIZ-off, "\n");
      }
      break;
    default: // HTML
      off+=snprintf(info+off, SINFOSIZ-off, DOCTYPE HTMLOPEN);
      off+=snprintf(info+off, SINFOSIZ-off, "<title>harvid server info</title></head>\n");
      off+=snprintf(info+off, SINFOSIZ-off, HTMLBODY);
      off+=snprintf(info+off, SINFOSIZ-off, CENTERDIV);
      off+=snprintf(info+off, SINFOSIZ-off, "<h2>harvid server info</h2>\n\n");
      off+=snprintf(info+off, SINFOSIZ-off, "<ul>\n");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Docroot: %s</li>\n", c->d->docroot);
      off+=snprintf(info+off, SINFOSIZ-off, "<li>ListenAddr: %s</li>\n", c->d->local_addr);
      off+=snprintf(info+off, SINFOSIZ-off, "<li>ListenPort: %d</li>\n", c->d->local_port);
      off+=snprintf(info+off, SINFOSIZ-off, "<li>CacheSize: %d</li>\n", initial_cache_size);
      off+=snprintf(info+off, SINFOSIZ-off, "<li>File Index: %s</li>\n", cfg_usermask & USR_INDEX ? "Yes" : "No");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Admin-task(s): /check%s%s%s</li>\n",
          (cfg_adminmask & ADM_FLUSHCACHE) ? " /flush_cache" : "",
          (cfg_adminmask & ADM_PURGECACHE) ? " /purge_cache" : "",
          (cfg_adminmask & ADM_SHUTDOWN)   ? " /shutdown" : ""
          );
#ifndef NDEBUG // possibly sensitive information
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Memlock: %s</li>\n", cfg_memlock ? "Yes" : "No");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Daemonized: %s</li>\n", cfg_daemonize ? "Yes" : "No");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Chroot: %s</li>\n", cfg_chroot ? cfg_chroot : "-");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>SetUid/Gid: %s/%s</li>\n",
          cfg_username ? cfg_username : "-", cfg_groupname ? cfg_groupname : "-");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Log: %s</li>\n",
          cfg_syslog ? "(syslog)" : (cfg_logfile ? cfg_logfile : "(stdout)"));
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Loglevel: %s</li>\n", dlog_level_name(debug_level));
      off+=snprintf(info+off, SINFOSIZ-off, "<li>AVLog (stdout): %s</li>\n",
          want_quiet ? "quiet" : want_verbose ? "verbose" :"error");
#endif
      off+=snprintf(info+off, SINFOSIZ-off, "</ul>\n</div>\n");
      off+=snprintf(info+off, SINFOSIZ-off, HTMLFOOTER, c->d->local_addr, c->d->local_port);
      off+=snprintf(info+off, SINFOSIZ-off, "</body>\n</html>");
      break;
  }
  return info;
}

char *hdl_server_version (CONN *c, ics_request_args *a) {
  char *info = malloc(SINFOSIZ * sizeof(char));
  int off = 0;
  switch (a->render_fmt) {
    case OUT_PLAIN:
      off+=snprintf(info+off, SINFOSIZ-off, "%s\n", SERVERVERSION);
      off+=snprintf(info+off, SINFOSIZ-off, "%s %s %s\n", LIBAVFORMAT_IDENT, LIBAVCODEC_IDENT, LIBAVUTIL_IDENT);
      break;
    case OUT_JSON:
      off+=snprintf(info+off, SINFOSIZ-off, "{");
      off+=snprintf(info+off, SINFOSIZ-off, "\"version\":\"%s\"", ICSVERSION);
      off+=snprintf(info+off, SINFOSIZ-off, ",\"os\":\"%s\"", ICSARCH);
#ifdef NDEBUG
      off+=snprintf(info+off, SINFOSIZ-off, ",\"debug\":false");
#else
      off+=snprintf(info+off, SINFOSIZ-off, ",\"debug\":true");
#endif
      off+=snprintf(info+off, SINFOSIZ-off, ",\"ffmpeg\":[\"%s\",\"%s\",\"%s\"]",
          LIBAVFORMAT_IDENT, LIBAVCODEC_IDENT, LIBAVUTIL_IDENT);
      off+=snprintf(info+off, SINFOSIZ-off, "}");
      break;
    case OUT_CSV:
      off+=snprintf(info+off, SINFOSIZ-off, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n",
          ICSVERSION, SERVERVERSION, LIBAVFORMAT_IDENT, LIBAVCODEC_IDENT, LIBAVUTIL_IDENT);
      break;
    default: // HTML
      off+=snprintf(info+off, SINFOSIZ-off, DOCTYPE HTMLOPEN);
      off+=snprintf(info+off, SINFOSIZ-off, "<title>harvid server version</title></head>\n");
      off+=snprintf(info+off, SINFOSIZ-off, HTMLBODY);
      off+=snprintf(info+off, SINFOSIZ-off, CENTERDIV);
      off+=snprintf(info+off, SINFOSIZ-off, "<h2>harvid server version</h2>\n\n");
      off+=snprintf(info+off, SINFOSIZ-off, "<ul>\n");
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Version: %s</li>\n", ICSVERSION);
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Operating System: %s</li>\n", ICSARCH);
#ifdef NDEBUG
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Debug enabled: No</li>\n");
#else
      off+=snprintf(info+off, SINFOSIZ-off, "<li>Debug enabled: Yes</li>\n");
#endif
      off+=snprintf(info+off, SINFOSIZ-off, "<li>ffmpeg: %s %s %s</li>\n", LIBAVFORMAT_IDENT, LIBAVCODEC_IDENT, LIBAVUTIL_IDENT);
      off+=snprintf(info+off, SINFOSIZ-off, "\n</ul>\n</div>\n");
      off+=snprintf(info+off, SINFOSIZ-off, HTMLFOOTER, c->d->local_addr, c->d->local_port);
      off+=snprintf(info+off, SINFOSIZ-off, "</body>\n</html>");
      break;
  }
  return info;
}

/////////////

int hdl_decode_frame(int fd, httpheader *h, ics_request_args *a) {
  VInfo ji;
  unsigned short vid;
  void *cptr = NULL;
  uint8_t *optr = NULL;
  size_t olen = 0;
  uint8_t *bptr = NULL;
  int err = 0;

  vid = dctrl_get_id(vc, dc, a->file_name);
  jvi_init(&ji);

  if (a->frame < 0) a->frame = 0; // return error instead?
  if (a->out_width < 0 || a->out_width > 16384) a->out_width = 0;
  if (a->out_height < 0 || a->out_height > 16384) a->out_height = 0;

  /* get canonical output width/height and corresponding buffersize */
  if ((err=dctrl_get_info_scale(dc, vid, &ji, a->out_width, a->out_height, a->decode_fmt)) || ji.buffersize < 1) {
    if (err == 503) {
      dlog(DLOG_WARNING, "VID: no decoder available (server overload).\n", fd);
      httperror(fd, 503, "Service Temporarily Unavailable", "<p>No decoder is available. The server is currently busy or overloaded.</p>");
    } else {
      dlog(DLOG_WARNING, "VID: no decoder available (invalid file or unsupported codec).\n", fd);
      httperror(fd, 500, "Service Unavailable", "<p>No decoder is available: File is invalid (no video track, unknown codec, invalid geometry,..)</p>");
    }
    return 0;
  }

  /* try encoded cache if a->render_fmt != FMT_RAW */
  if (a->render_fmt != FMT_RAW) {
     optr = icache_get_buffer(ic, vid, a->frame, a->render_fmt, a->misc_int, ji.out_width, ji.out_height, &olen, &cptr);
  }

  if (olen == 0) {
    /* get frame from cache - or decode it into the cache */
    bptr = vcache_get_buffer(vc, dc, vid, a->frame, ji.out_width, ji.out_height, a->decode_fmt, &cptr, &err);

    if (!bptr) {
      dlog(DLOG_ERR, "VID: error decoding video file for fd:%d err:%d\n", fd, err);
      if (err == 503) {
        httperror(fd, 503, "Service Temporarily Unavailable", "<p>Video cache is unavailable. The server is currently busy or overloaded.</p>");
      } else {
        httperror(fd, 500, "Service Unavailable", "<p>No decoder or cache is available: File is invalid (no video track, unknown codec, invalid geometry,..)</p>");
      }
      return 0;
    }

    switch (a->render_fmt) {
      case FMT_RAW:
        olen = ji.buffersize;
        optr = bptr;
        break;
      default:
        olen = format_image(&optr, a->render_fmt, a->misc_int, &ji, bptr);
        break;
    }
  }

  if(olen > 0 && optr) {
    debugmsg(DEBUG_ICS, "VID: sending %li bytes to fd:%d.\n", (long int) olen, fd);
    switch (a->render_fmt) {
      case FMT_RAW:
        h->ctype = "image/raw";
        break;
      case FMT_JPG:
        h->ctype = "image/jpeg";
        break;
      case FMT_PNG:
        h->ctype = "image/png";
        break;
      case FMT_PPM:
        h->ctype = "image/ppm";
        break;
      default:
        h->ctype = "image/unknown";
    }
    http_tx(fd, 200, h, olen, optr);

    if (bptr && a->render_fmt != FMT_RAW) {
      /* image was read from raw frame cache end encoded just now */
      if (icache_add_buffer(ic, vid, a->frame, a->render_fmt, a->misc_int, ji.out_width, ji.out_height, optr, olen)) {
        /* image was not added to image cache -> unreference the buffer */
        free(optr);
      } else if (! (cfg_usermask & USR_KEEPRAW)) {
        /* delete raw frame when encoded frame was cached */
        vcache_invalidate_buffer(vc, cptr);
      }
    }
  } else {
    dlog(DLOG_ERR, "VID: error formatting image for fd:%d\n", fd);
    httperror(fd, 500, NULL, NULL);
  }

  if (bptr)
    vcache_release_buffer(vc, cptr);
  else
    icache_release_buffer(ic, cptr);

  jvi_free(&ji);
  return (0);
}

void hdl_clear_cache() {
  vcache_clear(vc, -1);
  icache_clear(ic);
}

void hdl_purge_cache() {
  vcache_clear(vc, -1);
  icache_clear(ic);
  dctrl_cache_clear(vc, dc, 2, -1);
}

// vim:sw=2 sts=2 ts=8 et:
