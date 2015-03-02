/**
   @file daemon_util.h
   @brief common unix system utility functions

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
#ifndef _dutil_H
#define _dutil_H

#ifdef HAVE_WINDOWS
typedef int gid_t;
typedef int uid_t;
#else
#include <unistd.h>
#endif

/**
 * fork the current process in the background and close
 * standard-file descriptors.
 * @return 0 if successful, -1 on error (fork failed)
 */
int daemonize (void);


/**
 * resolve unix-user-name or ID to integer
 * @param setuid_user unix user name or ID
 * @return 0 on error, uid otherwise (root cannot be looked up)
 */
uid_t resolve_uid(const char *setuid_user);

/**
 * resolve unix-group-name or ID to integer
 * @param setgid_group unix group name or ID
 * @return 0 on error, gid otherwise (root cannot be looked up)
 */
gid_t resolve_gid(const char *setgid_group);

/**
 * assume a differernt user identity - drop root privileges.
 * @param uid unix user id
 * @param gid unix group id
 * @return 0 if successful, negative number on error
 */
int drop_privileges(const gid_t uid, const gid_t gid);

/**
 * change root - jail the daemon to confined path on the system
 * @param chroot_dir root directory of the server.
 * @return 0 if successful, negative number on error
 */
int do_chroot (char *chroot_dir);
#endif
