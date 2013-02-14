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

#include "daemon_log.h"

#define DEV_NULL "/dev/null"
int daemonize (void) {
#ifndef HAVE_WINDOWS
  switch(fork()) {
  case -1:    /* error */
    dlog(DLOG_CRIT,"SYS: fork() failed!\n");
    return -1;
  case  0:    /* child */
  #ifdef NOSETSID
    ioctl(0, TIOCNOTTY, 0) ;
  #else
    setsid();
  #endif
    chdir("/") ;
  #if 0
  if ((nulldev_fd = open(DEV_NULL, O_RDWR, 0)) != -1) {
    int i ;
    for (i = 0; i < 3; i++) {
      if (isatty(i)) {
        dup2(nulldev_fd, i) ;
      }
    }
    close(nulldev_fd) ;
  }
  #else
    fclose( stdin );
    fclose( stdout );
    fclose( stderr );
  #endif
    break;
  default:    /* parent */
    exit(0);
  }
#else
  dlog(DLOG_WARNING, "SYS: windows OS does not support daemon mode.\n");
#endif
  return 0;
}

/* chroot and set process user and group(s) id */
int drop_privileges(char *setgid_group, char *setuid_user) {
#ifndef HAVE_WINDOWS
  int uid=0, gid=0;
  struct group *gr;
  struct passwd *pw;

  if (getuid()) {
    dlog(DLOG_WARNING, "SYS: non-suid. keeping current privileges.\n");
    return 0;
  }

  /* Get the integer values */
  if(setgid_group) {
    gr=getgrnam(setgid_group);
    if(gr)
      gid=gr->gr_gid;
    else if(atoi(setgid_group)) /* numerical? */
      gid=atoi(setgid_group);
    else {
      dlog(DLOG_CRIT, "SYS: failed to get GID for group %s\n", setgid_group);
      return -1;
    }
  }
  if(setuid_user) {
    pw=getpwnam(setuid_user);
    if(pw)
      uid=pw->pw_uid;
    else if(atoi(setuid_user)) /* numerical? */
      uid=atoi(setuid_user);
    else {
      dlog(DLOG_CRIT, "SYS: failed to get UID for user %s\n", setuid_user);
      return -2;
    }
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
  dlog(DLOG_INFO,"SYS: privs now: uid:%i gid:%i\n", getuid(), getgid());
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
    dlog(DLOG_INFO, "SYS: chroot()ed to '%s'\n",chroot_dir);
  }
#else
  dlog(DLOG_WARNING, "SYS: windows OS does not support chroot() operation.\n");
#endif
  return 0;
}

// vim:sw=2 sts=2 ts=8 et:
