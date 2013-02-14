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

/**
 * fork the current process in the background and close
 * standard-file descriptors.
 * @return 0 if successful, -1 on error (fork failed)
 */
int daemonize (void);

/**
 * assume a differernt user identity - drop root privileges.
 * @param setgid_group unix group name
 * @param setuid_user  unix user name
 * @return 0 if successful, negative number on error
 */
int drop_privileges(char *setgid_group, char *setuid_user);

/**
 * change root - jail the daemon to confined path on the system
 * @param chroot_dir root directory of the server.
 * @return 0 if successful, negative number on error
 */
int do_chroot (char *chroot_dir);
#endif
