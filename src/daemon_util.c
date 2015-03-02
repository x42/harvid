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
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef HAVE_WINDOWS
#include <pwd.h>
#include <grp.h>
#endif

#include <dlog.h>
#include "daemon_util.h"

#define DEV_NULL "/dev/null"
int daemonize (void) {
#ifndef HAVE_WINDOWS
  switch(fork()) {
  case -1:    /* error */
    dlog(DLOG_CRIT, "SYS: fork() failed!\n");
    return -1;
  case  0:    /* child */
  #ifdef NOSETSID
    ioctl(0, TIOCNOTTY, 0) ;
  #else
    setsid();
  #endif
    chdir("/") ;
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    break;
  default:    /* parent */
    exit(0);
  }
#else
  dlog(DLOG_WARNING, "SYS: windows OS does not support daemon mode.\n");
#endif
  return 0;
}

uid_t resolve_uid(const char *setuid_user) {
#ifndef HAVE_WINDOWS
  int uid = 0;
  struct passwd *pw;
  if (!setuid_user) return 0;

  pw = getpwnam(setuid_user);
  if(pw)
    uid = pw->pw_uid;
  else if(atoi(setuid_user)) /* numerical? */
    uid = atoi(setuid_user);
  else {
    uid = 0;
  }
  if (uid == 0) {
    dlog(DLOG_CRIT, "SYS: invalid username '%s'\n", setuid_user);
  }
  return uid;
#else
  return 0;
#endif
}

gid_t resolve_gid(const char *setgid_group) {
#ifndef HAVE_WINDOWS
  gid_t gid = 0;
  struct group *gr;
  if(!setgid_group) return 0;
  gr = getgrnam(setgid_group);
  if(gr)
    gid = gr->gr_gid;
  else if(atoi(setgid_group)) /* numerical? */
    gid = atoi(setgid_group);
  else {
    gid = 0;
  }
  if (gid == 0) {
    dlog(DLOG_CRIT, "SYS: invalid group '%s'\n", setgid_group);
  }
  return gid;
#else
  return 0;
#endif
}

/* set process user and group(s) id */
int drop_privileges(const uid_t uid, const gid_t gid) {
#ifndef HAVE_WINDOWS
  if (getuid()) {
    dlog(DLOG_WARNING, "SYS: non-suid. Keeping current privileges.\n");
    return 0;
  }

  if (gid || uid) dlog(DLOG_INFO, "SYS: drop privileges; uid:%i gid:%i -> uid:%i gid:%i\n",
      getuid(), getgid(), uid, gid);

  /* Set uid and gid */
  if(gid) {
    if(setgid(gid)) {
      dlog(DLOG_CRIT, "SYS: setgid failed.\n");
      return -3;
    }
  }
  if(uid) {
    if(setuid(uid)) {
      dlog(DLOG_CRIT, "SYS: setuid failed.\n");
      return -4;
    }
  }
  dlog(DLOG_INFO, "SYS: privs now: uid:%i gid:%i\n", getuid(), getgid());
#else
  dlog(DLOG_WARNING, "SYS: windows OS does not support privilege uid/gid changes.\n");
#endif
  return 0;
}

int do_chroot (char *chroot_dir) {
#ifndef HAVE_WINDOWS
  if(chroot_dir) {
    if(chroot(chroot_dir)) {
      dlog(DLOG_CRIT, "SYS: Failed to chroot to '%s'\n", chroot_dir);
      return -1;
    }
    if(chdir("/")) {
      dlog(DLOG_CRIT, "SYS: Failed to chdir after chroot\n");
      return -2;
    }
    dlog(DLOG_INFO, "SYS: chroot()ed to '%s'\n", chroot_dir);
  }
#else
  dlog(DLOG_WARNING, "SYS: windows OS does not support chroot() operation.\n");
#endif
  return 0;
}

// vim:sw=2 sts=2 ts=8 et:
