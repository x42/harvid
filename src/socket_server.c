/*
   Copyright (C) 2002,2003,2008-2014 Robin Gareus <robin@gareus.org>

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
#include <string.h>
#include <errno.h>
#ifdef HAVE_WINDOWS
#include <windows.h>
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <signal.h>
#endif
#include <pthread.h>

#include "daemon_log.h"
#include "daemon_util.h"

#include "socket_server.h"

#ifndef uint8_t
#define uint8_t unsigned char
#endif

#ifndef HAVE_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#else
#ifndef socklen_t
#define socklen_t int
#endif
#endif

#ifndef HAVE_WINDOWS
#ifndef __APPLE__
#define HAVE_PTHREAD_SIGMASK
#endif
#define CATCH_SIGNALS
#endif

//#define VERBOSE_SHUTDOWN 1

/** called to spawn thread for an incoming connection */
static int create_client(void *(*cli)(void *), void *arg) {
  pthread_t thread;
#ifdef HAVE_PTHREAD_SIGMASK
  sigset_t newmask, oldmask;

  /* The idea is that only the main thread handles all the signals with
   * posix threads.  Signals are blocked for any other thread. */
  sigemptyset(&newmask);
  sigaddset(&newmask, SIGCHLD);
  sigaddset(&newmask, SIGPIPE); // ignore server loss - close on read.
  sigaddset(&newmask, SIGTERM);
  sigaddset(&newmask, SIGQUIT);
  sigaddset(&newmask, SIGINT);
  sigaddset(&newmask, SIGHUP);
//sigaddset(&newmask, SIGALRM);
  pthread_sigmask(SIG_BLOCK, &newmask, &oldmask); /* block signals */
#endif /* HAVE_PTHREAD_SIGMASK */
  pthread_attr_t pth_attr;
  pthread_attr_init(&pth_attr);
  pthread_attr_setdetachstate(&pth_attr, PTHREAD_CREATE_DETACHED);

  if(pthread_create(&thread, &pth_attr, cli, arg)) {
#ifdef HAVE_PTHREAD_SIGMASK
    pthread_sigmask(SIG_SETMASK, &oldmask, NULL); /* restore the mask */
#endif /* HAVE_PTHREAD_SIGMASK */
    return -1;
  }
#ifdef HAVE_PTHREAD_SIGMASK
  pthread_sigmask(SIG_SETMASK, &oldmask, NULL); /* restore the mask */
#endif /* HAVE_PTHREAD_SIGMASK */
  return 0;
}


/* -=-=-=-=-=-=-=-=-=-=- TCP socket daemon */

static void setnonblock(int sock, unsigned long l) {
#ifdef HAVE_WINDOWS
  //WSAAsyncSelect(sock, 0, 0, FD_CONNECT|FD_CLOSE|FD_WRITE|FD_READ|FD_OOB|FD_ACCEPT);
  //setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val,  sizeof(int));
  if(ioctlsocket(sock, FIONBIO, &l)<0)
#else
  if(ioctl(sock, FIONBIO, &l)<0)
#endif
    dlog(DLOG_WARNING, "SRV: unable to set (non)blocking mode: %s\n", strerror(errno));
  else
    debugmsg(DEBUG_SRV, "SRV: set fd:%d in %sblocking mode\n", sock, l ? "non-" : "");
}

static void server_sockaddr(ICI *d, struct sockaddr_in *addr) {
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = d->listenaddr;
  addr->sin_port = d->listenport;

  d->local_addr = strdup(inet_ntoa(addr->sin_addr));
  d->local_port = ntohs(d->listenport);
}


/** called once to init server */
static int create_server_socket(void) {
  int s, val = 1;
#ifdef HAVE_WINDOWS
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  if((s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
#else
  if((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
#endif
  {
    dlog(DLOG_CRIT, "SRV: unable to create local socket: %s\n", strerror(errno));
    return -1;
  }
  setnonblock(s, 1);
#ifndef HAVE_WINDOWS
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val,  sizeof(int));
#else
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*) &val,  sizeof(int));
#endif
  return(s);
}

/** called once after server socket has been created */
static int server_bind(ICI *d, struct sockaddr_in addr) {
  if(bind(d->fd, (struct sockaddr *)&addr, sizeof(addr))) {
    dlog(DLOG_CRIT, "SRV: Error binding to %s:%d\n", d->local_addr, d->local_port);
    return -1;
  }
  dlog(DLOG_INFO, "SRV: bound to %s:%d\n", d->local_addr, d->local_port);
  if(listen(d->fd, (MAXCONNECTIONS>>1))) {
    dlog(DLOG_CRIT, "SRV: Error listening on socket.\n");
    return -2;
  }
  return 0;
}

/* -=-=-=-=-=-=-=-=-=-=- TCP socket connection */
#define SLEEP_STEP (2)
//#define CON_TIMEOUT (cfg->timeout) // -- TODO - configuration param
//#define CON_TIMEOUT (30) // -- HTTP 30 sec
#define CON_TIMEOUT (300) // ICSP 5 min

static int global_shutdown = 0;
#ifdef CATCH_SIGNALS
void catchsig (int sig) {
  //signal(SIGHUP, catchsig); /* reset signal */
  //signal(SIGINT, catchsig);
  dlog(DLOG_INFO, "SRV: caught signal, shutting down\n");
  global_shutdown = 1;
}
#endif

/* this is the main client connection loop - one for each connection */
static void *socket_handler(void *cn) {
  CONN *c = (CONN*) cn;

  c->buf_len = 0;
  c->timeout_cnt = 0;
  debugmsg(DEBUG_SRV, "SRV: socket-handler starting up for fd:%d\n", c->fd);

  while(c->run && c->d->run) { // keep-alive

    fd_set rd_set, wr_set;
    struct timeval tv;

    tv.tv_sec = SLEEP_STEP;
    tv.tv_usec = 0;
    FD_ZERO(&rd_set);
    FD_ZERO(&wr_set);

    FD_SET(c->fd, &rd_set);
#ifdef SOCKET_WRITE
    if (c->cq != NULL) FD_SET(c->fd, &wr_set);
#endif

    int ready = select(c->fd+1, &rd_set, &wr_set, NULL, &tv);
    if(ready<0) {
      dlog(DLOG_WARNING, "SRV: connection select error: %s\n", strerror(errno));
      break;
    }
    if(!ready) { /* Timeout */
      c->timeout_cnt += SLEEP_STEP;
      if (c->timeout_cnt > CON_TIMEOUT) {
        dlog(DLOG_INFO, "SRV: connection timeout: connection reset\n");
        break;
      }
      continue;
    }

    // preform socket read/write on c->fd
    // NOTE: set c->run = 0; is preferred to return(!0) in protocol_handler;
    if (FD_ISSET(c->fd, &rd_set)) {
        debugmsg(DEBUG_SRV, "SRV: read..\n");
      if (protocol_handler(c, c->d->userdata)) break;
    }
#ifdef SOCKET_WRITE
    else  // check again if we can write now.
     if (FD_ISSET(c->fd, &wr_set)) {
      if (protocol_droid(c, c->d->userdata)) break;
    }
#endif
  debugmsg(DEBUG_SRV, "SRV: loop:%d\n", c->fd);

  }
  debugmsg(DEBUG_SRV, "SRV: protocol ended. closing connection fd:%d\n", c->fd);
#ifndef HAVE_WINDOWS
  close(c->fd);
#else
  closesocket(c->fd);
#endif

  pthread_mutex_lock(&c->d->lock);
  c->d->num_clients--;
  pthread_mutex_unlock(&c->d->lock);

  dlog(DLOG_INFO, "SRV: closed client connection (%u) from %s:%d.\n", c->fd, c->client_address, c->client_port);
  debugmsg(DEBUG_SRV, "SRV: now %i connections active\n", c->d->num_clients);

  if (c->client_address) free(c->client_address);
  free(c);
  return NULL; /* end close connection */
}

/**launch handler for each incoming connection. */
static void start_child(ICI *d, int fd, char *rh, unsigned short rp) {
  pthread_mutex_lock(&d->lock);
  d->num_clients++;
  if (d->num_clients > d->max_clients) d->max_clients = d->num_clients;
  pthread_mutex_unlock(&d->lock);

  CONN *c = calloc(1, sizeof(CONN));
  c->run = 1;
  c->fd = fd;
  c->d = d;
  c->client_address = strdup(rh);
  c->client_port = rp;
#ifdef SOCKET_WRITE
  c->cq = NULL;
#endif
  c->userdata = NULL;

  if(create_client(&socket_handler, c)) {
    if(fd >= 0)
#ifndef HAVE_WINDOWS
      close(fd);
#else
      closesocket(fd);
#endif
    dlog(DLOG_ERR, "SRV: Protocol handler child fork failed\n");
    pthread_mutex_lock(&d->lock);
    d->num_clients--;
    pthread_mutex_unlock(&d->lock);
    free(c);
    debugmsg(DEBUG_SRV, "SRV: Connection terminated: now %i connections active\n", d->num_clients);
    return;
  }
  debugmsg(DEBUG_SRV, "SRV: Connection started: now %i connections active\n", d->num_clients);
}

/** handshake - accept incoming connection  */
static int accept_connection(ICI *d, char **remotehost, unsigned short *rport) {
  struct sockaddr_in addr;
  int s;
  socklen_t addrlen = sizeof(addr);

  debugmsg(DEBUG_SRV, "SRV: waiting for accept on server-fd:%d\n", d->fd);

  do {
    s = accept(d->fd, (struct sockaddr *)&addr, &addrlen);
  } while(s < 0 && errno == EINTR);

  *remotehost = inet_ntoa(addr.sin_addr);
  *rport = ntohs(addr.sin_port);

  if(s<0) {
    dlog(DLOG_WARNING, "SRV: socket accept error: %s\n", strerror(errno));
    return (-1);
  }
  dlog(DLOG_INFO, "SRV: Connection accepted %s:%d\n", *remotehost, *rport);

  //  pthread_mutex_lock(&d->lock); ? not needed
  if (d->num_clients >= MAXCONNECTIONS) {
    protocol_error(s, 503, "Too many open connections. Please try again later.");
#ifndef HAVE_WINDOWS
    close(s);
#else
    closesocket(s);
#endif
    dlog(DLOG_WARNING, "SRV: refused client. max number of connections (%i) readed.\n", MAXCONNECTIONS);
    return (-1);
  }

  // check if we should use SO_KEEPALIVE here
  //int val = 1; setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &val,  sizeof(int));
  // or set non-blocking i/o ...
  //setnonblock(s, 1);
  return(s);
}

static int main_loop (void *arg) {
  ICI *d = arg;
  struct sockaddr_in addr;
  int rv = 0;
#ifndef HAVE_WINDOWS
  signal(SIGPIPE, SIG_IGN);
#endif

  if ((d->fd = create_server_socket()) < 0) {rv = -1; goto daemon_end;}
  server_sockaddr(d, &addr);
  if(server_bind(d, addr)) {rv = -1; goto daemon_end;}

  if (d->uid || d->gid) {
    if (drop_privileges(d->uid, d->gid)) {rv = -1; goto daemon_end;}
  }

  if (strlen(d->docroot) > 0 && access(d->docroot, R_OK)) {
    dlog(DLOG_CRIT, "SRV: can not read document-root (permission denied)\n");
    rv = -1;
    goto daemon_end;
  }

  global_shutdown = 0;
#ifdef CATCH_SIGNALS
  signal(SIGHUP, catchsig);
  signal(SIGINT, catchsig);
#endif

#ifdef USAGE_FREQUENCY_STATISTICS
  d->stat_start = time(NULL);
#endif

  while(d->run && !global_shutdown) {
    fd_set rfds;
    struct timeval tv;

    tv.tv_sec = 1; tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(d->fd, &rfds);

    // select() returns 0 on timeout, -1 on error.
    if((select(d->fd+1, &rfds, NULL, NULL, &tv))<0) {
      dlog(DLOG_WARNING, "SRV: unable to select the socket: %s\n", strerror(errno));
      if (errno != EINTR) {
        rv = -1;
        goto daemon_end;
      } else {
        continue;
      }
    }

    char *rh = NULL;
    unsigned short rp = 0;
    int s = -1;
    if(FD_ISSET(d->fd, &rfds)) {
      s = accept_connection(d, &rh, &rp);
    } else {
      d->age++;
#ifdef USAGE_FREQUENCY_STATISTICS
      /* may not be accurate, select() may skip a second once in a while */
      d->req_stats[time(NULL) % FREQ_LEN] = 0;
#endif
    }

    if (s >= 0) {
      start_child(d, s, rh, rp);
      d->age=0;
#ifdef USAGE_FREQUENCY_STATISTICS
      d->stat_count++;
      d->req_stats[time(NULL) % FREQ_LEN]++;
#endif
      continue; // no need to check age.
    }

    if (d->timeout > 0 && d->age > d->timeout) {
      dlog(DLOG_INFO, "SRV: no request since %d seconds shutting down.\n", d->age);
      global_shutdown = 1;
    }
  }

#ifdef CATCH_SIGNALS
  signal(SIGHUP, SIG_DFL);
  signal(SIGINT, SIG_DFL);
#endif

  /* wait until all connections are closed */
  int timeout = 31;

  if (d->num_clients > 0)
    dlog(DLOG_INFO, "SRV: server shutdown procedure: waiting up to %i sec for clients to disconnect..\n", timeout-1);

#ifdef VERBOSE_SHUTDOWN
  printf("\n");
#endif
  while (d->num_clients> 0 && --timeout > 0) {
#ifdef VERBOSE_SHUTDOWN
    if (timeout%3 == 0) printf("SRV: shutdown timeout (%i)    \r", timeout); fflush(stdout);
#endif
    mymsleep(1000);
  }
#ifdef VERBOSE_SHUTDOWN
  printf("\n");
#endif

  if (d->num_clients > 0) {
    dlog(DLOG_WARNING, "SRV: Terminating with %d active connections.\n", d->num_clients);
  } else {
    dlog(DLOG_INFO, "SRV: Closed all connections.\n");
  }

daemon_end:
  close(d->fd);
  dlog(DLOG_CRIT, "SRV: server shut down.\n");

  d->run = 0;
  if (d->local_addr) free(d->local_addr);
  pthread_mutex_destroy(&d->lock);
  free(d);
#ifdef HAVE_WINDOWS
  WSACleanup();
#endif
  return(rv);
}

// tcp server thread
int start_tcp_server (const unsigned int hostnl, const unsigned short port,
    const char *docroot, const uid_t uid, const gid_t gid,
    unsigned int timeout, void *userdata) {
  ICI *d = calloc(1, sizeof(ICI));
  pthread_mutex_init(&d->lock, NULL);
  d->run = 1;
  d->listenport = htons(port);
  d->listenaddr = hostnl;
  d->uid        = uid;
  d->gid        = gid;
  d->docroot    = docroot;
  d->age        = 0;
  d->timeout    = timeout;
  d->userdata   = userdata;
  return main_loop(d);
}

// vim:sw=2 sts=2 ts=8 et:
