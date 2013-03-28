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

#include <dlog.h>

#ifndef DAEMON_LOG_SELF
extern int debug_level; ///< global debug_level used by @ref dlog()
extern int debug_section; ///< global debug_level used by @ref dlog()
#endif

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

const char *dlog_level_name(int lvl);

#endif
