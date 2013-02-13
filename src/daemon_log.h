/**
   @file daemon_log.h
   @brief output and logfile abstraction

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
#ifndef _dlog_H
#define _dlog_H

/* some common win/posix issues */
#ifndef HAVE_WINDOWS
#define PRIlld "lld"
#define mymsleep(ms) usleep((ms) * 1000)
#else
#define PRIlld "I64d"
#define mymsleep(ms) Sleep(ms)
#endif

/* syslog */
#ifndef HAVE_WINDOWS
#include <syslog.h>
#else
#include <windows.h>
#include <winsock.h>

#define LOG_CRIT 0
#define LOG_ERR 1
#define LOG_WARNING 2
#define LOG_INFO 3
#define LOG_DEBUG 4
#endif

/* log Levels */
#define DLOG_CRIT        LOG_CRIT    ///< critical conditions
#define DLOG_ERR         LOG_ERR     ///< error conditions
#define DLOG_WARNING     LOG_WARNING ///< warning conditions
#define DLOG_INFO        LOG_INFO    ///< informational
#define DLOG_DEBUG       LOG_DEBUG   ///< debug-level messages


#ifndef DAEMON_LOG_SELF
extern int debug_level; ///< global debug_level used by @ref dlog()
#endif

/**
 * printf replacement
 *
 * @param level  log level 0-7  see syslog.h
 * @param format same as printf(...)
 */
void dlog(int level, const char *format, ...);

/**
 * initialise dlog output.
 * if log_file is NULL - syslog is used - otherwise messages are
 * written to the specified file. - if dlog_open is not called
 * dlog writes to stdout (>DLOG_WARNING) or stderr (Warnings, Errors).
 *
 * @param log_file file-name or NULL
 */
void dlog_open(char *log_file);

/**
 * dlog_close() can be called anytime. It closes syslog or log-files if they
 * were opened. After closing the log, dlog() will write to stdout/stderr.
 */
void dlog_close(void);

#endif
