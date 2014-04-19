/*
   This file is part of harvid

   Copyright (C) 2002,2003,2008-2014 Robin Gareus <robin@gareus.org>

   This file contains GPL code from mini-http, micro-http and libcurl.
   by Jef Poskanzer <jef@mail.acme.com> and
   Daniel Stenberg, <daniel@haxx.se>.

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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#include <dlog.h>
#include "socket_server.h"
#include "httprotocol.h"
#include "htmlconst.h"
#include "ics_handler.h"

/* -=-=-=-=-=-=-=-=-=-=- HTTP helper functions */

const char * send_http_status_fd (int fd, int status) {
  char http_head[128];
  const char *title;
  switch (status) {
    case 200: title = "OK"; break;
  //case 302: title = "Found"; break;
  //case 304: title = "Not Modified"; break;
    case 400: title = "Bad Request"; break;
  //case 401: title = "Unauthorized"; break;
    case 403: title = "Forbidden"; break;
    case 404: title = "Not Found"; break;
    case 415: title = "Unsupported Media Type"; break;
  //case 408: title = "Request Timeout"; break;
    case 500: title = "Internal Server Error"; break;
    case 501: title = "Not Implemented"; break;
    case 503: title = "Service Temporarily Unavailable"; break;
    default:  title = "Internal Server Error"; status = 500; break;
  }
  snprintf(http_head, sizeof(http_head), "%s %d %s\015\012", PROTOCOL, status, title);
  CSEND(fd, http_head);
  return title;
}

#define HTHSIZE (1024)

void send_http_header_fd(int fd , int s, httpheader *h) {
  char hd[HTHSIZE];
  int off = 0;
  time_t now;
  char timebuf[100];

  now = time(NULL);
  strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
  off += snprintf(hd+off, HTHSIZE-off, "Date: %s\n", timebuf);
  off += snprintf(hd+off, HTHSIZE-off, "Server: %s\r\n", SERVERVERSION);

  if (h && h->ctype)
    off += snprintf(hd+off, HTHSIZE-off, "Content-type: %s\r\n", h->ctype);
  else
    off += snprintf(hd+off, HTHSIZE-off, "Content-type: text/html; charset=UTF-8\r\n");
  if (h && h->encoding)
    off += snprintf(hd+off, HTHSIZE-off, "Content-Encoding: %s\r\n", h->encoding);
  if (h && h->extra)
    off += snprintf(hd+off, HTHSIZE-off, "%s\r\n", h->extra);
  if (h && h->length > 0)
#ifdef HAVE_WINDOWS
    off += snprintf(hd+off, HTHSIZE-off, "Content-Length:%lu\r\n", (unsigned long) h->length);
#else
    off += snprintf(hd+off, HTHSIZE-off, "Content-Length:%zu\r\n", h->length);
#endif
  if (h && h->retryafter)
    off += snprintf(hd+off, HTHSIZE-off, "Retry-After:%s\r\n", h->retryafter);
  else if (s == 503)
    off += snprintf(hd+off, HTHSIZE-off, "Retry-After:5\r\n");
  if (h && h->mtime) {
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&h->mtime));
    off += snprintf(hd+off, HTHSIZE-off, "Last-Modified: %s\r\n", timebuf);
  }

  off += snprintf(hd+off, HTHSIZE-off, "Connection: close\r\n");
  off += snprintf(hd+off, HTHSIZE-off, "\r\n");
  CSEND(fd, hd);
}

void httperror(int fd , int s, const char *title, const char *str) {
  char hd[HTHSIZE];
  int off = 0;

  const char *t = send_http_status_fd(fd, s);
  send_http_header_fd(fd, s, NULL);

  if (!title) title = t;
  off += snprintf(hd+off, HTHSIZE-off, DOCTYPE HTMLOPEN);
  off += snprintf(hd+off, HTHSIZE-off, "<title>Error %i %s</title></head>", s, title);
  off += snprintf(hd+off, HTHSIZE-off, "<body><h1>%s</h1>", title);

  if (str && strlen(str)>0) {
    off += snprintf(hd+off, HTHSIZE-off, "<p>%s</p>\r\n", str);
  } else {
    off += snprintf(hd+off, HTHSIZE-off, "<p>%s</p>\r\n", "Sorry.");
  }
  off += snprintf(hd+off, HTHSIZE-off, ERRFOOTER);
  CSEND(fd, hd);
}

int http_tx(int fd, int s, httpheader *h, size_t len, const uint8_t *buf) {
  h->length = len;
  send_http_status_fd(fd, s);
  send_http_header_fd(fd, s, h);

  // select
  #define WRITE_TIMEOUT (50) // TODO make configurable
  int timeout = WRITE_TIMEOUT;
  size_t offset = 0;
  while (timeout > 0) {
    fd_set rd_set, wr_set;
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    FD_ZERO(&rd_set);
    FD_ZERO(&wr_set);
    FD_SET(fd, &wr_set);
    int ready = select(fd+1, &rd_set, &wr_set, NULL, &tv);
    if(ready < 0) return (-1); // error
    if(!ready) timeout--;
    else {
#ifndef HAVE_WINDOWS
      int rv = write(fd, buf+offset, len-offset);
      debugmsg(DEBUG_HTTP, "  written (%d/%zu) @%zu on fd:%i\n", rv, len-offset, offset, fd);
#else
      int rv = send(fd, (const char*) (buf+offset), (size_t) (len-offset), 0);
      debugmsg(DEBUG_HTTP, "  written (%d/%lu) @%lu on fd:%i\n", rv, (unsigned long)(len-offset), (unsigned long) offset, fd);
#endif
      if (rv < 0) {
        dlog(DLOG_WARNING, "HTTP: write to socket failed: %s\n", strerror(errno));
        break; // TODO: don't break on EAGAIN, ENOBUFS, ENOMEM or similar
      } else if (rv != len-offset) {
         dlog(DLOG_WARNING, "HTTP: short-write (%u/%u) @%u on fd:%i\n", rv, len-offset, offset, fd);
         timeout = WRITE_TIMEOUT;
         offset += rv;
      } else {
         offset += rv;
        break;
      }
    }
  }
  if (!timeout)
    dlog(DLOG_ERR, "HTTP: write timeout fd:%i\n", fd);

  if (offset != len) {
    dlog(DLOG_WARNING, "HTTP: write to fd:%d failed at (%u/%u) = %.2f%%\n", fd, offset, len, (float)offset*100.0/(float)len);
    return (1);
  }
  return (0);
}

// from libcurl - thanks to GPL and Daniel Stenberg <daniel@haxx.se>
char *url_escape(const char *string, int inlength) {
  if (!string) return strdup("");
  size_t alloc = (inlength?(size_t)inlength:strlen(string))+1;
  char *ns;
  char *testing_ptr = NULL;
  unsigned char in; /* we need to treat the characters unsigned */
  size_t newlen = alloc;
  int strindex = 0;
  size_t length;

  ns = malloc(alloc);
  if(!ns) return NULL;

  length = alloc-1;
  while(length--) {
    in = *string;

    switch (in) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j':
    case 'k': case 'l': case 'm': case 'n': case 'o':
    case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E':
    case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
      ns[strindex++] = in;
      break;
    default:
      newlen += 2; /* the size grows with two, since this'll become a %XX */
      if(newlen > alloc) {
        alloc *= 2;
        testing_ptr = realloc(ns, alloc);
        if(!testing_ptr) {
          free(ns);
          return NULL;
        }
        else {
          ns = testing_ptr;
        }
      }
      snprintf(&ns[strindex], 4, "%%%02X", in);
      strindex += 3;
      break;
    }
    string++;
  }
  ns[strindex] = 0; /* terminate it */
  return ns;
}

#include <ctype.h>
#define ISXDIGIT(x) (isxdigit((int) ((unsigned char)x)))

// also adapted from libcurl
char *url_unescape(const char *string, int length, int *olen) {
  if (!string) return strdup("");
  int alloc = (length?length:(int)strlen(string))+1;
  char *ns = malloc(alloc);
  unsigned char in;
  int strindex = 0;
  long hex;

  if(!ns) return NULL;

  while(--alloc > 0) {
    in = *string;
    if(('%' == in) && ISXDIGIT(string[1]) && ISXDIGIT(string[2])) {
      /* this is two hexadecimal digits following a '%' */
      char hexstr[3];
      char *ptr;
      hexstr[0] = string[1];
      hexstr[1] = string[2];
      hexstr[2] = 0;

      hex = strtol(hexstr, &ptr, 16);

      in = (unsigned char)hex; /* this long is never bigger than 255 anyway */

      string += 2;
      alloc -= 2;
    }

    ns[strindex++] = in;
    string++;
  }
  ns[strindex] = 0; /* terminate it */

    /* store output size */
  if(olen) *olen = strindex;
  return ns;
}


/* -=-=-=-=-=-=-=-=-=-=- protocol handler implementation */

/*
 * HTTP protocol handler implements virtual
 * int protocol_error(int fd, int status);
 */
void protocol_error(int fd, int status, char *msg) {
  httperror(fd, status, "Error", msg?msg:"Unspecified Error.");
}

void protocol_response(int fd, char *msg) {
  send_http_status_fd(fd, 200); \
  send_http_header_fd(fd, 200, NULL); \
  CSEND(fd, msg);
}

/* parse HTTP protocol header */
static char *get_next_line(char **str) {
  char *t = *str;
  char *xx = strpbrk(t, "\n\r");
  if (!xx) return (NULL);
  *xx++ = '\0';
  while (xx && (*xx == '\r' || *xx == '\n')) xx++;
  *str = xx;
  return t;
}

/* check accept for image/png[;..] */
static int compare_accept(char *line) {
  int rv = 0;
  char *tmp;
  if ((tmp = strchr(line, ';'))) *tmp = '\0'; // ignore opt. parameters
  if (!strncmp(line, "image/", 6)) {
    rv |= 1;
    debugmsg(DEBUG_HTTP, "HTTP: accept image: %s\n", line);
  } else if (!strcmp(line, "*/*")) {
    rv |= 2;
    debugmsg(DEBUG_HTTP, "HTTP: accept all: %s\n", line);
  }
  if (tmp) *tmp = ';';
  return rv;
}

/*
 * HTTP protocol handler implements virtual
 * int protocol_handler(fd_set rd_set, CONN *c);
 * for: HTTP & ics-query
 */
int protocol_handler(CONN *c, void *unused) {
#ifndef HAVE_WINDOWS
  int num = read(c->fd, c->buf, BUFSIZ);
#else
  int num = recv(c->fd, c->buf, BUFSIZ, 0);
#endif
  if (num < 0 && (errno == EINTR || errno == EAGAIN)) return(0);
  if (num < 0) return(-1);
  if (num == 0) return(-1); // end of input
  c->buf[num] = '\0';

#if 0 // non HTTP commands - security issue
  if (!strncmp(c->buf, "quit", 4)) {c->run = 0; return(0);}
  else if (!strncmp(c->buf, "shutdown", 8)) { c->d->run = 0; return(0);}
#endif

  debugmsg(DEBUG_HTTP, "HTTP: CON raw-input: '%s'\n", c->buf);

  char *method_str;
  char *path, *protocol, *query;

  /* Parse the first line of the request. */
  method_str = c->buf;
  if (method_str == (char*) 0) {
    httperror(c->fd, 400, "Bad Request", "Can't parse request method."); c->run = 0; return(0);
  }
  path = strpbrk(method_str, " \t\012\015");
  if (path == (char*) 0) {
    httperror(c->fd, 400, "Bad Request", "Can't parse request path."); c->run = 0; return(0);
  }
  *path++ = '\0';
  path += strspn(path, " \t\012\015");
  protocol = strpbrk(path, " \t\012\015");
  if (protocol == (char*) 0) {
    httperror(c->fd, 400, "Bad Request", "Can't parse request protocol."); c->run = 0; return(0);
  }
  *protocol++ = '\0';
  protocol += strspn(protocol, " \t\012\015");

  query = strchr(path, '?');
  if (query == (char*) 0)
    query = "";
  else
  *query++ = '\0';

  char *header = strpbrk(protocol, "\n\r \t\012\015");
  if (!header && strncmp(protocol, "HTTP/0.9", 8)) {
    httperror(c->fd, 400, "Bad Request", "Can't parse request header.");
    c->run = 0; return(0);
  } else if (!header)
    header = "";
  else {
    *header++ = '\0';
    while (header && (*header == '\r' || *header == '\n')) header++;
  }

#ifdef HAVE_WINDOWS
  debugmsg(DEBUG_HTTP, "HTTP: CON header-len: %lu\n", (long unsigned) strlen(header));
#else
  debugmsg(DEBUG_HTTP, "HTTP: CON header-len: %zu\n", strlen(header));
#endif
  debugmsg(DEBUG_HTTP, "HTTP: CON header: ''%s''\n", header);


  char *cookie = NULL, *host = NULL, *referer = NULL, *useragent = NULL;
  char *contenttype = NULL, *accept = NULL; long int contentlength = 0;
  char *cp, *line;

  /* Parse the rest of the request headers. */
  while ((line = get_next_line(&header)))
  {
    if (line[0] == '\0')
        break;
    else if (strncasecmp(line, "Accept:", 7) == 0)
        {
        cp = &line[7];
        cp += strspn(cp, " \t");
        accept = cp;
        }
    else if (strncasecmp(line, "Cookie:", 7) == 0)
        {
        cp = &line[7];
        cp += strspn(cp, " \t");
        cookie = cp;
        }
    else if (strncasecmp(line, "Host:", 5) == 0)
      {
        cp = &line[5];
        cp += strspn(cp, " \t");
        host = cp;
        if (strchr(host, '/') != (char*) 0 || host[0] == '.') {
          httperror(c->fd, 400, "Bad Request", "Can't parse request.");
          c->run = 0; return(0);
        }
      }
    else if (strncasecmp(line, "Referer:", 8) == 0)
        {
        cp = &line[8];
        cp += strspn(cp, " \t");
        referer = cp;
        }
    else if (strncasecmp(line, "User-Agent:", 11) == 0)
        {
        cp = &line[11];
        cp += strspn(cp, " \t");
        useragent = cp;
        }
    else if (strncasecmp(line, "Content-Type:", 13) == 0)
        {
        cp = &line[13];
        cp += strspn(cp, " \t");
        contenttype = cp;
        }
    else if (strncasecmp(line, "Content-Length:", 15) == 0)
        {
        cp = &line[15];
        cp += strspn(cp, " \t");
        contentlength = atoll(cp);
        }
    else
        debugmsg(DEBUG_HTTP, "HTTP: CON header not parsed: '%s'\n", line);

  }
  debugmsg(DEBUG_HTTP, "HTTP: CON header co='%s' ho='%s' re='%s' ua='%s' ac='%s'\n",
     cookie, host, referer, useragent, accept);

  /* process headers */

  int ac = accept?0:-1;
  line = accept;
  while (line && (cp = strchr(line, ','))) {
    *cp = '\0';
    ac |= compare_accept(line);
    line = cp+1;
  }
  if (line)
    ac |= compare_accept(line);

  if (ac == 0) {
    httperror(c->fd, 415, "", "Your client does not accept any files that this server can produce.\n");
    c->run = 0;
    return(0);
  }

  debugmsg(DEBUG_CON, "HTTP: Proto: '%s', method: '%s', path: '%s' query:'%s'\n", protocol, method_str, path, query);

  /* pre-process request */
  if (!strcmp("POST", method_str)
      && (contenttype && !strcmp(contenttype, "application/x-www-form-urlencoded"))
      && (contentlength > 0 && contentlength <= strlen(header) /* - (header-c->buf) */ /* -num */)
      && (num < BUFSIZ)
      ) {
      header[contentlength] = '\0';
#ifdef HAVE_WINDOWS
      debugmsg(DEBUG_CON, "HTTP: translate POST->GET query - length data:%lu cl:%ld\n", (long unsigned) strlen(header), contentlength);
#else
      debugmsg(DEBUG_CON, "HTTP: translate POST->GET query - length data:%zu cl:%ld\n", strlen(header), contentlength);
#endif
      debugmsg(DEBUG_CON, "HTTP: x-www-form-urlencoded:'%s'\n", header);
      query = header;
      method_str = "GET";
  }

  /* process request */
  ics_http_handler(c, host, protocol, path, method_str, query, cookie);

  return(0);
}

// vim:sw=2 sts=2 ts=8 et:
