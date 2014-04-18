/**
   @file httprotocol.h
   @brief HTTP protocol

   This file is part of harvid

   @author Robin Gareus <robin@gareus.org>
   @copyright

   Copyright (C) 2002,2003,2008-2013 Robin Gareus <robin@gareus.org>

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
#ifndef _HTTPROTOCOL_H
#define _HTTPROTOCOL_H

#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#ifdef HAVE_WINDOWS
#include <windows.h>
#include <winsock.h>
#endif

#define DOCTYPE "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
#define HTMLOPEN "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head><meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\" />\n"

#define PROTOCOL "HTTP/1.0" ///< HTTP protocol version for replies
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT" ///< time format used in HTTP header

#ifdef HAVE_WINDOWS
#define SL_SEP(string) (strlen(string)>0?(string[strlen(string)-1]=='/' || string[strlen(string)-1]=='\\')?"":"\\":"")
#else
#define SL_SEP(string) (strlen(string)>0?(string[strlen(string)-1]=='/')?"":"/":"")
#endif

#ifndef uint8_t
#define uint8_t unsigned char
#endif
#ifndef socklen_t
#define socklen_t int
#endif

#ifndef HAVE_WINDOWS
#define CSEND(FD,MSG) write(FD, MSG, strlen(MSG))
#else
#define CSEND(FD,MSG) send(FD, MSG, strlen(MSG), 0)
#endif


/**
 * @brief HTTP header
 *
 * interanl representation of a HTTP header to be sent with http_tx()
 */
typedef struct {
  size_t length; ///< Content-Length (default: 0 - not sent)
  time_t mtime;  ///< Last-Modified  (default: 0 - don't send this header)
  char  *extra;  ///< any additional HTTP header "key:value" - if not NULL '\\r\\n' is appended to this string.
  char  *encoding; ///< Content-Encoding (default: NUll - not sent)
  char  *ctype; ///< Content-type (default: text/html)
  char  *retryafter; ///< for 503 errors: Retry-After time value in seconds (default: 5)
} httpheader;

/**
 * send a HTTP error reply.
 * @param fd socket file descriptor
 * @param s HTTP status code
 * @param title optional HTTP status-code message (may be NULL)
 * @param str optional text body explaining the error (may be NULL)
 */
void httperror(int fd , int s, const char *title, const char *str);

/**
 * send HTTP reply status, header and transmit data.
 * @param fd socket file descriptor
 * @param s HTTP status code (usually 200)
 * @param h HTTP header information to send
 * @param len number of bytes to send
 * @param buf data to send
 */
int http_tx(int fd, int s, httpheader *h, size_t len, const uint8_t *buf);

/**
 * internal, private function to send the HTTP status line
 * @param fd socket file descriptor
 * @param status HTTP status code
 */
const char * send_http_status_fd (int fd, int status);

/**
 * internal function to format and send HTTP header
 * @param fd socket file descriptor
 * @param s HTTP status code
 * @param h HTTP header information to send
 */
void send_http_header_fd(int fd , int s, httpheader *h);

/**
 */
char *url_unescape(const char *string, int length, int *olen);

/**
 */
char *url_escape(const char *string, int inlength);
#endif
