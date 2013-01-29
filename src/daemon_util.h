/**
 * @file daemon_util.h
 * @author Robin Gareus <robin@gareus.org>
 * @brief common unix system utility functions
 */
#ifndef _dutil_H
#define _dutil_H

/**
 * fork the current process in the background and close
 * standard-file descriptors.
 */
void daemonize (void);

/**
 * assume a differernt user identity - drop root privileges.
 * @param setgid_group unix group name
 * @param setuid_user  unix user name
 */
void drop_privileges(char *setgid_group, char *setuid_user);

/**
 * change root - jail the daemon to confined path on the system
 * @param chroot_dir root directory of the server.
 */
void do_chroot (char *chroot_dir);
#endif
