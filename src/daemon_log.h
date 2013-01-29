/**
 * @file daemon_log.h
 * @author Robin Gareus <robin@gareus.org>
 * @brief output and logfile abstraction
 */
#ifndef _dlog_H
#define _dlog_H

#ifndef HAVE_WINDOWS
#include <syslog.h>
#else
#include <windows.h>
#include <winsock.h>
#warning "NO SYSLOG"
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
