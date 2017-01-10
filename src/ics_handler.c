/*
   This file is part of harvid

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
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <dlog.h>
#include <ffcompat.h> // harvid.h
#include "httprotocol.h"
#include "ics_handler.h"
#include "htmlconst.h"
#include "enums.h"

extern int cfg_usermask;
extern int cfg_adminmask;

/** Compare Transport Protocol request */
#define CTP(CMPPATH) \
  (  strncasecmp(protocol,  "HTTP/", 5) == 0 \
  && strncasecmp(path, CMPPATH, strlen(CMPPATH)) == 0 \
  && strcasecmp (method_str, "GET") == 0)

#define SEND200(MSG) \
  send_http_status_fd(c->fd, 200); \
  send_http_header_fd(c->fd, 200, NULL); \
  CSEND(c->fd, MSG);

#define SEND200CT(MSG,CT) \
  { \
    httpheader h; \
    memset(&h, 0, sizeof(httpheader)); \
    h.ctype = CT; \
    send_http_status_fd(c->fd, 200); \
    send_http_header_fd(c->fd, 200, &h); \
    CSEND(c->fd, MSG); \
  }

#define CONTENT_TYPE_SWITCH(fmt) \
   (fmt) == OUT_PLAIN ? "text/plain" : \
  ((fmt) == OUT_JSON  ? "application/json" : \
  ((fmt) == OUT_CSV   ? "text/csv" : "text/html; charset=UTF-8"))

/**
 * check for invalid or potentially malicious path.
 */
static int check_path(char *f) {
  int len;
  if (!f) return -1;
  len = strlen(f);
  if (len == 0) return 0;
  /* check for possible 'escape docroot' trickery */
  if (   f[0] == '/'
      || strcmp(f, "..") == 0 || strncmp( f, "../", 3) == 0
      || strstr(f, "/../") != (char*) 0
      || strcmp(&(f[len-3]), "/..") == 0) return -1;
  return 0;
}

///////////////////////////////////////////////////////////////////

struct queryparserstate {
  ics_request_args *a;
  char *fn;
  int doit;
};

void parse_param(struct queryparserstate *qps, char *kvp) {
  char *sep;
  if (!(sep = strchr(kvp, '='))) return;
  *sep = '\0';
  char *val = sep+1;
  if (!val || strlen(val) < 1 || strlen(kvp) <1) return;

  debugmsg(DEBUG_ICS, "QUERY '%s'->'%s'\n", kvp, val);

  if (!strcmp (kvp, "frame")) {
    qps->a->frame = atoi(val);
    qps->doit |= 1;
  } else if (!strcmp (kvp, "w")) {
    qps->a->out_width  = atoi(val);
  } else if (!strcmp (kvp, "h")) {
    qps->a->out_height = atoi(val);
  } else if (!strcmp (kvp, "file")) {
    qps->fn = url_unescape(val, 0, NULL);
    qps->doit |= 2;
  } else if (!strcmp (kvp, "flatindex")) {
    qps->a->idx_option |= OPT_FLAT;
  } else if (!strcmp (kvp, "format")) {
         if (!strncmp(val, "jpg",3))  {qps->a->render_fmt = FMT_JPG; qps->a->misc_int = atoi(&val[3]);}
    else if (!strncmp(val, "jpeg",4)) {qps->a->render_fmt = FMT_JPG; qps->a->misc_int = atoi(&val[4]);}
    else if (!strcmp(val, "png"))      qps->a->render_fmt = FMT_PNG;
    else if (!strcmp(val, "ppm"))      qps->a->render_fmt = FMT_PPM;
    else if (!strcmp(val, "yuv"))     {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_YUV420P;}
    else if (!strcmp(val, "yuv420"))  {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_YUV420P;}
    else if (!strcmp(val, "yuv440"))  {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_YUV440P;}
    else if (!strcmp(val, "yuv422"))  {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_YUYV422;}
    else if (!strcmp(val, "uyv422"))  {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_UYVY422;}
    else if (!strcmp(val, "rgb"))     {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_RGB24;}
    else if (!strcmp(val, "bgr"))     {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_BGR24;}
    else if (!strcmp(val, "rgba"))    {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_RGBA;}
    else if (!strcmp(val, "argb"))    {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_ARGB;}
    else if (!strcmp(val, "bgra"))    {qps->a->render_fmt = FMT_RAW; qps->a->decode_fmt = AV_PIX_FMT_BGRA;}
    /* info, version, rc,... format */
    else if (!strcmp(val, "html"))    qps->a->render_fmt = OUT_HTML;
    else if (!strcmp(val, "xhtml"))   qps->a->render_fmt = OUT_HTML;
    else if (!strcmp(val, "json"))    qps->a->render_fmt = OUT_JSON;
    else if (!strcmp(val, "csv"))     qps->a->render_fmt = OUT_CSV;
    else if (!strcmp(val, "plain"))   qps->a->render_fmt = OUT_PLAIN;
  }
}

static void parse_http_query_params(struct queryparserstate *qps, char *query) {
  char *t, *s = query;
  while(s && (t = strpbrk(s, "&?"))) {
    *t = '\0';
    parse_param(qps, s);
    s = t+1;
  }
  if (s) parse_param(qps, s);
}

static int parse_http_query(CONN *c, char *query, httpheader *h, ics_request_args *a) {
  struct queryparserstate qps = {a, NULL, 0};

  a->decode_fmt = AV_PIX_FMT_RGB24;
  a->render_fmt = FMT_PNG;
  a->frame = 0;
  a->misc_int = 0;
  a->out_width = a->out_height = -1; // auto-set

  parse_http_query_params(&qps, query);

  /* check for illegal paths */
  if (!qps.fn || check_path(qps.fn)) {
    httperror(c->fd, 404, "File not found.", "File not found.");
    return(-1);
  }

  /* sanity checks */
  if (qps.doit&3) {
    if (qps.fn) {
      a->file_name = malloc(1+strlen(c->d->docroot)+strlen(qps.fn)*sizeof(char));
      sprintf(a->file_name, "%s%s", c->d->docroot, qps.fn);
#ifdef HAVE_WINDOWS
      char *tmp;
      while (tmp = strchr(a->file_name, '/')) *tmp = '\\';
#endif
      a->file_qurl = qps.fn;
    }

    /* test if file exists or send 404 */
    struct stat sb;
    if (stat(a->file_name, &sb)) {
      dlog(DLOG_WARNING, "CON: file not found: '%s'\n", a->file_name);
      httperror(c->fd, 404, "Not Found", "file not found.");
      return(-1);
    }

    /* check file permissions */
    if (access(a->file_name, R_OK)) {
      dlog(DLOG_WARNING, "CON: permission denied for file: '%s'\n", a->file_name);
      httperror(c->fd, 403, NULL, NULL);
      return(-1);
    }

    if (h) h->mtime = sb.st_mtime;

    debugmsg(DEBUG_ICS, "serving '%s' f:%"PRId64" @%dx%d\n", a->file_name, a->frame, a->out_width, a->out_height);
  }
  return qps.doit;
}

/////////////////////////////////////////////////////////////////////
// Callbacks -- request handlers

// harvid.c
int   hdl_decode_frame (int fd, httpheader *h, ics_request_args *a);
char *hdl_homepage_html (CONN *c);
char *hdl_server_status_html (CONN *c);
char *hdl_file_info (CONN *c, ics_request_args *a);
char *hdl_file_seek (CONN *c, ics_request_args *a);
char *hdl_server_info (CONN *c, ics_request_args *a);
char *hdl_server_version (CONN *c, ics_request_args *a);
void  hdl_clear_cache();
void  hdl_purge_cache();

// fileindex.c
void hdl_index_dir (int fd, const char *root, char *base_url, const char *path, int fmt, int opt);

// logo.o

#ifdef XXDI

#define EXTLD(NAME) \
  extern const unsigned char ___ ## NAME []; \
  extern const unsigned int ___ ## NAME ## _len;
#define LDVAR(NAME) ___ ## NAME
#define LDLEN(NAME) ___ ## NAME ## _len

#elif defined __APPLE__
#include <mach-o/getsect.h>

#define EXTLD(NAME) \
  extern const unsigned char _section$__DATA__ ## NAME [];
#define LDVAR(NAME) _section$__DATA__ ## NAME
#define LDLEN(NAME) (getsectbyname("__DATA", "__" #NAME)->size)

#elif (defined HAVE_WINDOWS)  /* mingw */

#define EXTLD(NAME) \
  extern const unsigned char binary____ ## NAME ## _start[]; \
  extern const unsigned char binary____ ## NAME ## _end[];
#define LDVAR(NAME) \
  binary____ ## NAME ## _start
#define LDLEN(NAME) \
  ((binary____ ## NAME ## _end) - (binary____ ## NAME ## _start))

#else /* gnu ld */

#define EXTLD(NAME) \
  extern const unsigned char _binary____ ## NAME ## _start[]; \
  extern const unsigned char _binary____ ## NAME ## _end[];
#define LDVAR(NAME) \
  _binary____ ## NAME ## _start
#define LDLEN(NAME) \
  ((_binary____ ## NAME ## _end) - (_binary____ ## NAME ## _start))
#endif

EXTLD(doc_harvid_jpg)
EXTLD(doc_seek_js)

/////////////////////////////////////////////////////////////////////

/** main http request handler / dispatch requests */
void ics_http_handler(
  CONN *c,
  char *host, char *protocol,
  char *path, char *method_str,
  char *query, char *cookie
  ) {

  if (CTP("/status")) {
    char *status = hdl_server_status_html(c);
    SEND200(status);
    free(status);
    c->run = 0;
  } else if (CTP("/favicon.ico")) {
    #include "favicon.h"
    httpheader h;
    memset(&h, 0, sizeof(httpheader));
    h.ctype = "image/x-icon";
    h.length = sizeof(favicon_data);
    h.mtime = 1361225638 ; // TODO compile time check image timestamp
    http_tx(c->fd, 200, &h, sizeof(favicon_data), favicon_data);
    c->run = 0;
  } else if (CTP("/logo.jpg")) {
    httpheader h;
    memset(&h, 0, sizeof(httpheader));
    h.ctype = "image/jpeg";
    h.length = LDLEN(doc_harvid_jpg);
    h.mtime = 1361225638 ; // TODO compile time check image timestamp
    http_tx(c->fd, 200, &h, h.length, LDVAR(doc_harvid_jpg));
    c->run = 0;
  } else if ((cfg_usermask & USR_WEBSEEK) && CTP("/seek.js")) {
    httpheader h;
    memset(&h, 0, sizeof(httpheader));
    h.ctype = "application/javascript";
    h.length = LDLEN(doc_seek_js);
    h.mtime = 1361225638 ; // TODO compile time check image timestamp
    http_tx(c->fd, 200, &h, h.length, LDVAR(doc_seek_js));
    c->run = 0;
  } else if ((cfg_usermask & USR_WEBSEEK) && CTP("/seek")) {
    ics_request_args a;
    memset(&a, 0, sizeof(ics_request_args));
    int rv = parse_http_query(c, query, NULL, &a);
    if (rv < 0) {
      ;
    } else if (rv&2) {
      char *info = hdl_file_seek(c, &a);
      if (info) {
        SEND200(info);
        free(info);
      }
    } else {
      httperror(c->fd, 400, "Bad Request", "<p>Insufficient query parameters.</p>");
    }
    if (a.file_name) free(a.file_name);
    if (a.file_qurl) free(a.file_qurl);
    c->run = 0;
  } else if (CTP("/info")) { /* /info -> /file/info !! */
    ics_request_args a;
    memset(&a, 0, sizeof(ics_request_args));
    int rv = parse_http_query(c, query, NULL, &a);
    if (rv < 0) {
      ;
    } else if (rv&2) {
      char *info = hdl_file_info(c, &a);
      if (info) {
        SEND200CT(info, CONTENT_TYPE_SWITCH(a.render_fmt));
        free(info);
      }
    } else {
      httperror(c->fd, 400, "Bad Request", "<p>Insufficient query parameters.</p>");
    }
    if (a.file_name) free(a.file_name);
    if (a.file_qurl) free(a.file_qurl);
    c->run = 0;
  } else if (CTP("/rc")) {
    ics_request_args a;
    struct queryparserstate qps = {&a, NULL, 0};
    memset(&a, 0, sizeof(ics_request_args));
    parse_http_query_params(&qps, query);
    char *info = hdl_server_info(c, &a);
    SEND200CT(info, CONTENT_TYPE_SWITCH(a.render_fmt));
    free(info);
    free(qps.fn);
    c->run = 0;
  } else if (CTP("/version")) {
    ics_request_args a;
    struct queryparserstate qps = {&a, NULL, 0};
    memset(&a, 0, sizeof(ics_request_args));
    parse_http_query_params(&qps, query);
    char *info = hdl_server_version(c, &a);
    SEND200CT(info, CONTENT_TYPE_SWITCH(a.render_fmt));
    free(info);
    free(qps.fn);
    c->run = 0;
  } else if (CTP("/index/")) { /* /index/  -> /file/index/ ?! */
    struct stat sb;
    char *dp = url_unescape(&(path[7]), 0, NULL);
    char *abspath = malloc((strlen(c->d->docroot) + strlen(dp) + 2) * sizeof(char));
    sprintf(abspath, "%s%s%s", c->d->docroot, strlen(c->d->docroot) > 0 ? "/" : "", dp);
#ifdef HAVE_WINDOWS
      char *tmp;
      while (tmp = strchr(abspath, '/')) *tmp = '\\';
      if (strlen (abspath) > 0 && abspath[strlen(abspath) - 1] == '\\') {
        abspath[strlen(abspath) - 1] = '\0';
      }
#endif
    if (! (cfg_usermask & USR_INDEX)) {
      httperror(c->fd, 403, NULL, NULL);
    } else if (!dp || check_path(dp)) {
      httperror(c->fd, 400, "Bad Request", "Illegal filename.");
    } else if (stat(abspath, &sb) || !S_ISDIR(sb.st_mode)) {
      dlog(DLOG_WARNING, "CON: dir not found: '%s'\n", abspath);
      httperror(c->fd, 404, "Not Found", "file not found.");
    } else if (access(abspath, R_OK)) {
      dlog(DLOG_WARNING, "CON: permission denied for dir: '%s'\n", abspath);
      httperror(c->fd, 403, NULL, NULL);
    } else {
      debugmsg(DEBUG_ICS, "indexing dir: '%s'\n", abspath);
      ics_request_args a;
      char base_url[1024];
      struct queryparserstate qps = {&a, NULL, 0};
      memset(&a, 0, sizeof(ics_request_args));
      parse_http_query_params(&qps, query);
      snprintf(base_url, 1024, "http://%s%s", host, path);
      if (! (cfg_usermask & USR_FLATINDEX)) a.idx_option &= ~OPT_FLAT;
      SEND200CT("", CONTENT_TYPE_SWITCH(a.render_fmt));
      hdl_index_dir(c->fd, c->d->docroot, base_url, dp, a.render_fmt, a.idx_option);
      free(dp);
      free(qps.fn);
    }
    free(abspath);
    c->run = 0;
  } else if (CTP("/admin")) { /* /admin/ */
    if (strncasecmp(path,  "/admin/check", 12) == 0) {
      SEND200("ok\n");
    } else if (strncasecmp(path,  "/admin/flush_cache", 18) == 0) {
      if (cfg_adminmask & ADM_FLUSHCACHE) {
        hdl_clear_cache();
        SEND200(OK200MSG("cache flushed\n"));
      } else {
        httperror(c->fd, 403, NULL, NULL);
      }
    } else if (strncasecmp(path,  "/admin/purge_cache", 18) == 0) {
      if (cfg_adminmask & ADM_PURGECACHE) {
        hdl_purge_cache();
        SEND200(OK200MSG("cache purged\n"));
      } else {
        httperror(c->fd, 403, NULL, NULL);
      }
    } else if (strncasecmp(path,  "/admin/shutdown", 15) == 0) {
      if (cfg_adminmask & ADM_SHUTDOWN) {
        SEND200(OK200MSG("shutdown queued\n"));
        c->d->run = 0;
      } else {
        httperror(c->fd, 403, NULL, NULL);
      }
    } else {
      httperror(c->fd, 400, "Bad Request", "Nonexistent admin command.");
    }
    c->run = 0;
  } else if (CTP("/") && !strcmp(path, "/") && strlen(query) == 0) { /* HOMEPAGE */
    char *msg = hdl_homepage_html(c);
    SEND200(msg);
    free(msg);
    c->run = 0;
  }
  else if (  (strncasecmp(protocol,  "HTTP/", 5) == 0) /* /?file= -> /file/frame?.. !! */
           &&(strcasecmp (method_str, "GET") == 0)
           )
  {
    ics_request_args a;
    httpheader h;
    memset(&a, 0, sizeof(ics_request_args));
    memset(&h, 0, sizeof(httpheader));
    int rv = parse_http_query(c, query, &h, &a);
    if (rv < 0) {
      ;
    } else if (rv == 3) {
      hdl_decode_frame(c->fd, &h, &a);
    } else {
      httperror(c->fd, 400, "Bad Request", "<p>Insufficient query parameters.</p>");
    }
    if (a.file_name) free(a.file_name);
    if (a.file_qurl) free(a.file_qurl);
    c->run = 0;
  }
  else
  {
    httperror(c->fd, 500, "", "server does not know what to make of this.\n");
    c->run = 0;
  }
}

// vim:sw=2 sts=2 ts=8 et:
