/**
   @file dlog.h
   @brief output and logfile abstraction

   This file is part of harvid

   @author Robin Gareus <robin@gareus.org>
   @copyright

   Copyright (C) 2002,2003,2008-2014 Robin Gareus <robin@gareus.org>

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
#ifndef _harvid_dlog_H
#define _harvid_dlog_H

/* some common win/posix issues */
#define PRIlld "lld"

#ifndef _WIN32
#define mymsleep(ms) usleep((ms) * 1000)
#define SNPRINTF snprintf

#else

#include <windows.h>
#define mymsleep(ms) Sleep(ms)
int portable_snprintf(char *str, size_t str_m, const char *fmt, /*args*/ ...);
#define SNPRINTF portable_snprintf
#endif

/* syslog */
#ifndef _WIN32
#include <syslog.h>
#else
#define LOG_EMERG 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_INFO 6
#endif

/* log Levels */
#define DLOG_EMERG       LOG_EMERG    ///< quiet (system is unusable)
#define DLOG_CRIT        LOG_CRIT    ///< critical conditions -- usually implies exit() or process termination
#define DLOG_ERR         LOG_ERR     ///< error conditions -- recoverable errors
#define DLOG_WARNING     LOG_WARNING ///< warning conditions
#define DLOG_INFO        LOG_INFO    ///< informational

enum {DEBUG_SRV=1, DEBUG_HTTP=2, DEBUG_CON=4, DEBUG_DCTL=8, DEBUG_ICS=16};

#ifndef DAEMON_LOG_SELF
extern int debug_level; ///< global debug_level used by @ref dlog()
extern int debug_section; ///< global debug_level used by @ref dlog()
#endif

/**
 * printf replacement
 *
 * @param level  log level 0-7  see syslog.h
 * @param format same as printf(...)
 */
void dlog(int level, const char *format, ...);

#ifdef NDEBUG
#define debugmsg(section, ...) {}
#else
#define debugmsg(section, ...) {if (debug_section&section) printf(__VA_ARGS__);}
#endif

#define raprintf(p, off, siz, ...) \
{ \
  if (siz - off < 256) { siz *= 2; p = realloc(p, siz * sizeof(char)); } \
  off += snprintf(p + off, siz - off, __VA_ARGS__); \
}

#define rpprintf(p, off, siz, ...) \
{ \
  while ((*siz) - (*off) <= SNPRINTF((*p) + (*off), 0, __VA_ARGS__)) \
  { (*siz) *= 2; (*p) = realloc(*p, (*siz) * sizeof(char)); } \
  (*off) += snprintf((*p) + (*off), (*siz) - (*off), __VA_ARGS__); \
  assert((*siz) >= (*off)); \
}

#define rprintf(...) rpprintf(m,o,s, __VA_ARGS__)

#endif

