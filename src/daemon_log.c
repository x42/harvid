/*
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
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> // getpid

#define DAEMON_LOG_SELF
#include "daemon_log.h"

int debug_level = DLOG_INFO;
int debug_section = 0;

int use_syslog = 0; //internal
FILE *my_logfile = NULL; //internal
#define LOGLEN (1024)

void dlog(int level, const char *format, ...) {
  va_list arglist;
  char text[LOGLEN], timestamped[LOGLEN];
  FILE *out = stderr;
  int dotimestamp = 0;

  if(level > debug_level) return;

  va_start(arglist, format);
  vsnprintf(text, LOGLEN, format, arglist);
  va_end(arglist);
  text[LOGLEN -1] = 0;

  if (level > 7) { level = 7; }
  if (level < 0) { level = 0; }
#ifndef HAVE_WINDOWS
  if(use_syslog) {
    syslog(level, "%s", text);
    return;
  } else
#endif
  if (my_logfile) {
    out = my_logfile;
    dotimestamp = 1;
  } else if (level > DLOG_WARNING) {
    out = stdout;
  }

  if(dotimestamp) {
    struct tm *timeptr;
    time_t now;
    now = time(NULL);
    now = mktime(gmtime(&now));
    now += mktime(localtime(&now)) - mktime(gmtime(&now)); // localtime
    timeptr = localtime(&now);

    snprintf(timestamped, LOGLEN,
      "%04d.%02d.%02d %02d:%02d:%02d LOG%d[%lu]: %s",
      timeptr->tm_year+1900, timeptr->tm_mon+1, timeptr->tm_mday,
      timeptr->tm_hour, timeptr->tm_min, timeptr->tm_sec,
      level, (unsigned long) getpid(), text);
  } else {
    snprintf(timestamped, LOGLEN, "%s", text);
  }
  timestamped[LOGLEN -1] = 0;

  fprintf(out, "%s", timestamped);
  fflush(out); // may kill performance
}

void dlog_open(char *log_file) {
  int fd;

#ifndef HAVE_WINDOWS
  if (!log_file) {
    openlog("harvid", LOG_CONS | LOG_NDELAY | LOG_PID, 0 ? LOG_DAEMON : LOG_USER);
    use_syslog = 1;
    return;
  }
#endif

  if ((fd = open(log_file, O_CREAT|O_WRONLY|O_APPEND, 0640))) {
#ifndef HAVE_WINDOWS
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    my_logfile = fdopen(fd, "a");
    if (my_logfile) return;
  }
  dlog(DLOG_ERR, "Unable to open log file: '%s'\n", log_file);
}

void dlog_close(void) {
    if(my_logfile) { fclose(my_logfile); my_logfile = NULL; return; }
#ifndef HAVE_WINDOWS
    if(use_syslog) closelog();
#endif
}

const char *dlog_level_name(int lvl) {
  switch (lvl) {
    case DLOG_EMERG:
      return "quiet";
    case DLOG_CRIT:
      return "critical";
    case DLOG_ERR:
      return "error";
    case DLOG_WARNING:
      return "warning";
    case DLOG_INFO:
      return "info";
    default:
      return "-";
  }
}

// vim:sw=2 sts=2 ts=8 et:
