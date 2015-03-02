/**
   @file socket_server.h
   @brief TCP socket interface and daemon

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
#ifndef _SOCKETSERVER_H
#define _SOCKETSERVER_H

#include <stdio.h>
#include <pthread.h>

// limit number of connections per daemon
#define MAXCONNECTIONS (120)

#ifndef NDEBUG
#define USAGE_FREQUENCY_STATISTICS 1
#endif

#if (defined USAGE_FREQUENCY_STATISTICS && !defined FREQ_LEN)
#define FREQ_LEN (3600)
#endif

#ifdef HAVE_WINDOWS
typedef int gid_t;
typedef int uid_t;
#else
#include <unistd.h>
#endif

/**
 * @brief daemon
 *
 * Daemon handle and configuration
 */
typedef struct ICI {
  int fd;  ///< file descriptor of the socket
  int run; ///< server status: 1= keep running , 0 = error/end/terminate.
  unsigned short listenport; ///< in network order notation
  unsigned int listenaddr;   ///< in network order notation
  int  local_port; ///< same as listeport  - in host order notation
  char *local_addr;///< same as listenaddr - in host order notation
  int num_clients; ///< current number of connected clients
  int max_clients; ///< configured max. number of connections for this server
  pthread_mutex_t lock; ///< lock to modify num_clients
  uid_t uid;       ///< drop privileges, assume this userid
  gid_t gid;       ///< drop privileges, adopt this group
  const char *docroot;   ///< document root for all connections
  unsigned int age; ///< used for timeout -- in seconds
  unsigned int timeout; ///< if > 0 shut down serve if age reaches this value
  void *userdata;  ///< generic placeholder for usage specific data
#ifdef USAGE_FREQUENCY_STATISTICS
  time_t       req_stats[FREQ_LEN];
  time_t       stat_start;
  unsigned int stat_count;
#endif
} ICI;

/**
 * @brief client connection
 *
 * client connection handle
 */
typedef struct CONN {
  ICI *d; ///< pointer to parent daemon
  int fd; ///< file descriptor of the connection
  short run; ///< connection status: 1= keep running , 0 = error/end/terminate.
  char buf[BUFSIZ]; ///< Socket read buffer
  int buf_len; ///< Index of first unused byte in buf
  int timeout_cnt; ///< internal connectiontimeout counter
  char *client_address;///< IP address of the client
  unsigned short client_port; ///< port used by the client
#ifdef SOCKET_WRITE
  void *cq; ///< outgoing command queue
#endif
  void *userdata; ///< generic information for this connection
} CONN;


/**
 * @brief allocates and initializes an ICI structure and enters the server thread.
 *
 * While the tcp server itself launches threads for each incoming connection,
 * start_tcp_server() does not return until this server has been shut down.
 * launching a server will activate the connection callbacks \ref protocol_handler()
 * and \ref protocol_droid().
 *
 * @param hostnl listen IP in network byte order. eg htonl(INADDR_ANY)
 * @param port TCP port to listen on
 * @param docroot configure the document-root for all connections to this server.
 * @param uid specify the user-id that the server will assume. If \a uid is zero no suid is performed.
 * @param gid the unix group of the server; \a gid may be zero in which case the effective group ID of the calling process will remain unchanged.
 * @param d user-data passed on to callbacks.
 */
int start_tcp_server (const unsigned int hostnl, const unsigned short port,
		const char *docroot, const uid_t uid, const gid_t gid,
		unsigned int timeout, void *d);

// extern function virtual prototype(s)
/**
 * virtual callback - implement this for the server's protocol.
 *
 * this callback will be invoked if data is available for reading on the socket.
 *
 * @param c socket connection to handle
 * @param d user/application specific server-data from \ref start_tcp_server()
 * @return return 0 is no error occured, returning non zero will close the connection.
 */
int protocol_handler(CONN *c, void *d); // called for each incoming data.

/**
 * virtual callback - implement this for the server's protocol.
 *
 * @param fd socket connection to handle
 * @param status some status number
 * @param msg may be NULL
 * @param status integer - here: HTTP status code
 */
void protocol_error(int fd, int status, char *msg);

/**
 * virtual callback - implement this for the server's protocol.
 */
void protocol_response(int fd, char *msg);

/**
 * virtual callback - implement this for the server's protocol.
 *
 * this callback will be executed when data can be written to the socket. but only if SOCKET_WRITE is defined
 *
 * @param c socket connection to handle
 * @param d user/application specific server-data from \ref start_tcp_server()
 * @return return 0 is no error occured, returning non zero will close the connection.
 */
int protocol_droid(CONN *c, void *d); // called if socket is writable and c->cq is not NULL

#endif
