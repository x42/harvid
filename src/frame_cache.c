/*
   This file is part of harvid

   Copyright (C) 2008-2013 Robin Gareus <robin@gareus.org>

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
#include <stdint.h>     /* uint8_t */
#include <inttypes.h>
#include <stdlib.h>     /* calloc et al.*/
#include <string.h>     /* memset */
#include <unistd.h>

#include "decoder_ctrl.h"
#include "daemon_log.h"
#include "frame_cache.h"
#include "ffcompat.h"
#include "ffdecoder.h"

#include <time.h>
#include <assert.h>
#include <pthread.h>

/* FLAGS */
#define CLF_DECODING 1 //< decoder is active
#define CLF_INUSE 2    //< currently being served
#define CLF_VALID 4    //< cacheline is valid (has decoded frame)

typedef struct videocacheline {
  int id;         // file ID from VidMap
  int w;
  int h;
  int fmt;        // pixel format
  int64_t frame;
  int flags;
  int refcnt;     // CLF_INUSE reference count
  time_t lru;     // east recently used time
  //int hitcount  //  -- unused; least-frequently used idea
  uint8_t *b;     //< data buffer pointer
  struct videocacheline *next; // TODO: if cache grows large we may want to use tree-structure
} videocacheline;

/* create a new cacheline and append it */
static videocacheline *newcl(videocacheline *cache) {
  videocacheline *cl = calloc(1, sizeof(videocacheline));
  cl->frame = -1;
  cl->fmt = PIX_FMT_NONE;
  //append new line to cache
  videocacheline *cptr = cache;
  while (cptr && cptr->next) cptr = cptr->next;
  if (cptr) cptr->next = cl;
  return(cl);
}

/* free and clear a cacheline */
static int freecl(videocacheline *cptr) {
  if (!cptr) return (1);
  if ((cptr->flags&(CLF_DECODING|CLF_INUSE))) return(1);

  assert(cptr->refcnt == 0);
  free(cptr->b); cptr->b = NULL;
  cptr->lru = 0; cptr->frame = -1;
  cptr->flags = 0; cptr->fmt = PIX_FMT_NONE;
  return (0);
}

/* get a new cacheline or replace and existing one
 * NB. the cache needs to be write-locked when calling this
 * and realloccl_buf() must be called after this
 */
static videocacheline *getcl(videocacheline *cache, int cfg_cachesize) {
  int i = 0;
  videocacheline *over = NULL; // if cache is full - this one will be flushed and re-used
  time_t lru = time(NULL) + 1;
  videocacheline *cptr = cache;

  while (cptr) {
    if (cptr->flags == 0) return (cptr);
    if (!(cptr->flags&(CLF_DECODING|CLF_INUSE)) && (cptr->lru < lru))  {
      lru = cptr->lru;
      over = cptr;
    }
    cptr = cptr->next;
    i++;
  }

  if (i < cfg_cachesize)
    return(newcl(cache));

  cptr = over; /* replace LRU */
  if (cptr && !(cptr->flags&(CLF_DECODING|CLF_INUSE))) {
    cptr->flags = 0;
    cptr->lru = 0; cptr->frame = -1;
    /* retain w, h and fmt - we can keep allocated buffer
     * -> call realloccl_buf() after this fn. */
    assert(cptr->refcnt == 0);
    return (cptr);
  }

  /* all cache lines are in USE/locked */
  dlog(DLOG_WARNING, "CACHE: cache full - all cache-lines in use.\n");
  return (NULL);
}

/* check if requested data exists in cache */
static videocacheline *testclwh(videocacheline *cache, int64_t frame, int w, int h, int fmt, int id) {
  videocacheline *cptr = cache;
#ifdef USE_MEMCMP
  const videocacheline cmp = {id, w, h, fmt, frame, 0, 0, 0, NULL, NULL};
  const size_t cs = 4 * sizeof(int) + sizeof(int64_t);
#endif

  while (cptr) {
#ifdef USE_MEMCMP
    if (!memcmp(cptr, &cmp, cs))
#else
    if (   cptr->frame == frame
        && cptr->w == w
        && cptr->h == h
        && cptr->id == id
        && cptr->fmt == fmt
       )
#endif
      return(cptr);
    cptr = cptr->next;
  }
  return(NULL);
}

/* clear cache
 * if f==2 force also to flush data (even if it's in USE)
 * if f==1 wait for cachelines to become unused
 * if f==0 the cache is flushed but data objects are only freed next
 * time a cacheline is needed
 */
static void clearcache(videocacheline *cache, pthread_mutex_t *cachelock, int f) {
  videocacheline *cptr = cache;
  videocacheline *prev = cache;
  while (cptr) {
    videocacheline *mem = cptr;
    if (f > 1) {
      /* we really should not do this */
      cptr->flags &= ~(CLF_DECODING|CLF_INUSE);
      cptr->refcnt = 0; // XXX may trigger assert() in vcache_release_buffer()
    } else if (f) {
      if (cptr->flags & (CLF_DECODING|CLF_INUSE)) {
	dlog(DLOG_WARNING, "CACHE: waiting for cacheline to be unlocked.\n");
      }
      while (cptr->flags & (CLF_DECODING|CLF_INUSE)) {
	pthread_mutex_unlock(cachelock);
	mymsleep(5);
	pthread_mutex_lock(cachelock);
      }
    }
    if (freecl(cptr)) {
      prev = cptr;
      cptr = cptr->next;
    } else {
      cptr = cptr->next;
      prev->next = cptr;
      if (mem != cache) free(mem);
    }
  }
  if (f) cache->next = NULL;
}

static void realloccl_buf(videocacheline *cptr, int w, int h, int fmt) {
  if (cptr->b && cptr->w == w && cptr->h == h && cptr->fmt == fmt)
    return; // already allocated

  free(cptr->b);
  cptr->b = calloc(picture_bytesize(fmt, w, h), sizeof(uint8_t));
  cptr->w = w;
  cptr->h = h;
  cptr->fmt = fmt;
}

///////////////////////////////////////////////////////////////////////////////
// Cache Control

typedef struct {
  int cfg_cachesize;
  videocacheline *vcache;
  pthread_mutex_t lock;
  int cache_hits;
  int cache_miss;
} xjcd;

static void fc_initialize_cache (xjcd *cc) {
  assert(!cc->vcache);
  cc->vcache = newcl(NULL);
  cc->cache_hits = 0;
  cc->cache_miss = 0;
  pthread_mutex_init(&cc->lock, NULL);
}

static void fc_flush_cache (xjcd *cc) {
  pthread_mutex_lock(&cc->lock);
  clearcache(cc->vcache, &cc->lock, 1);
  cc->cache_hits = 0;
  cc->cache_miss = 0;
  pthread_mutex_unlock(&cc->lock);
}

static videocacheline *fc_readcl(xjcd *cc, void *dc, int64_t frame, int w, int h, int fmt, int vid) {
  /* check if the requested frame is cached */
  videocacheline *rv = testclwh(cc->vcache, frame, w, h, fmt, vid);
  if (rv) {
    /* found data in cache */
    pthread_mutex_lock(&cc->lock);
    /* check if it has been recently invalidated by another thread */
    if (rv->flags&CLF_VALID) {
      rv->refcnt++;
      rv->flags |= CLF_INUSE;
      pthread_mutex_unlock(&cc->lock);
      rv->lru = time(NULL);
      cc->cache_hits++;
      return(rv);
    }
    pthread_mutex_unlock(&cc->lock);
    rv = NULL;
  }

  /* too bad, now we need to allocate a new or free an used
   * cacheline and then decode the video... */
  int timeout = 250; /* 1 second to get a buffer */
  do {
    pthread_mutex_lock(&cc->lock);
    rv = getcl(cc->vcache, cc->cfg_cachesize);
    if (rv) {
      rv->flags |= CLF_DECODING;
    }
    pthread_mutex_unlock(&cc->lock);
    if (!rv) {
      mymsleep(5);
    }
  } while(--timeout > 0 && !rv);

  if (!rv) {
    /* no buffer available */
    return NULL;
  }

  /* set w,h,fmt and re-alloc buffer if neccesary */
  realloccl_buf(rv, w, h, fmt);

  /* fill cacheline with data - decode video */
  if (dctrl_decode(dc, vid, frame, rv->b, w, h, fmt)) {
    /* we don't cache decode-errors */
    pthread_mutex_lock(&cc->lock);
    rv->flags &= ~CLF_VALID;
    rv->flags &= ~CLF_DECODING;
    rv->flags |= CLF_INUSE;
    rv->refcnt++;
    pthread_mutex_unlock(&cc->lock);
    dlog(DLOG_WARNING, "CACHE: decode failed.\n");
    return (rv); // this is OK, a black frame will have been rendered
  }

  rv->id = vid;
  rv->frame = frame;
  rv->lru = time(NULL);
  pthread_mutex_lock(&cc->lock);
  rv->flags |= CLF_VALID|CLF_INUSE;
  rv->flags &= ~CLF_DECODING;
  rv->refcnt++;
  pthread_mutex_unlock(&cc->lock);
  cc->cache_miss++;
  return(rv);
}

///////////////////////////////////////////////////////////////////////////////
// public API

void vcache_clear (void *p) {
  xjcd *cc = (xjcd*) p;
  pthread_mutex_lock(&cc->lock);
  clearcache(cc->vcache, &cc->lock, 0);
  cc->cache_hits = 0;
  cc->cache_miss = 0;
  pthread_mutex_unlock(&cc->lock);
}

void vcache_create(void **p) {
  (*((xjcd**)p)) = (xjcd*) calloc(1, sizeof(xjcd));
  (*((xjcd**)p))->cfg_cachesize = 48;
  fc_initialize_cache((*((xjcd**)p)));
}

void vcache_resize(void **p, int size) {
  if (size < (*((xjcd**)p))->cfg_cachesize)
    fc_flush_cache((*((xjcd**)p)));
  if (size > 0)
    (*((xjcd**)p))->cfg_cachesize = size;
}

void vcache_destroy(void **p) {
  xjcd *cc = *(xjcd**) p;
  fc_flush_cache(cc);
  pthread_mutex_destroy(&cc->lock);
  free(cc->vcache);
  free(cc);
  *p = NULL;
}

uint8_t *vcache_get_buffer(void *p, void *dc, int id, int64_t frame, int w, int h, int fmt, void **cptr) {
  videocacheline *cl = fc_readcl((xjcd*)p, dc, frame, w, h, fmt, id);
  if (!cl) {
    if (cptr) *cptr = NULL;
    return NULL;
  }
  if (cptr) *cptr = cl;
  return cl->b;
}

void vcache_release_buffer(void *p, void *cptr) {
  xjcd *cc = (xjcd*) p;
  videocacheline *cl = (videocacheline *)cptr;
  if (!cptr) return;
  pthread_mutex_lock(&cc->lock);
  if (--cl->refcnt < 1) {
    assert(cl->refcnt >= 0);
    cl->flags &= ~CLF_INUSE;
  }
  pthread_mutex_unlock(&cc->lock);
}

///////////////////////////////////////////////////////////////////////////////
// statistics

static char *flags2txt(int f) {
  char *rv = NULL;
  size_t off = 0;

  if (f == 0) {
    rv = (char*) realloc(rv, (off+2) * sizeof(char));
    off += sprintf(rv+off, "-");
    return rv;
  }
  if (f&CLF_DECODING) {
    rv = (char*) realloc(rv, (off+10) * sizeof(char));
    off += sprintf(rv+off, "decoding ");
  }
  if (f&CLF_VALID) {
    rv = (char*) realloc(rv, (off+7) * sizeof(char));
    off += sprintf(rv+off, "valid ");
  }
  if (f&CLF_INUSE) {
    rv = (char*) realloc(rv, (off+8) * sizeof(char));
    off += sprintf(rv+off, "in-use ");
  }
  return rv;
}

size_t vcache_info_html(void *p, char *m, size_t n) {
  size_t off = 0;
  videocacheline *cptr = ((xjcd*)p)->vcache;
  off += snprintf(m+off, n-off, "<h3>Cache Info:</h3>\n");
  off += snprintf(m+off, n-off, "<p>Size: max. %i entries.\n", ((xjcd*)p)->cfg_cachesize);
  off += snprintf(m+off, n-off, "Hits: %d, Misses: %d</p>\n", ((xjcd*)p)->cache_hits, ((xjcd*)p)->cache_miss);
  off += snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off += snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Flags</th><th>W</th><th>H</th><th>Buffer</th><th>Frame#</th><th>LRU</th></tr>\n");
  int i = 0;
  while (cptr) {
    char *tmp = flags2txt(cptr->flags);
    off += snprintf(m+off, n-off,
        "<tr><td>%d</td><td>%d</td><td>%s</td><td>%d</td><td>%d</td><td>%s</td><td>%"PRId64"</td><td>%"PRIlld"</td></tr>\n",
	i, cptr->id, tmp, cptr->w, cptr->h, (cptr->b ? ff_fmt_to_text(cptr->fmt) : "null"), cptr->frame, (long long) cptr->lru);
    free(tmp);
    i++;
    cptr = cptr->next;
  }
  off += snprintf(m+off, n-off, "</table>\n");
  return(off);
}

void vcache_info_dump(void *p) {
  videocacheline *cptr = ((xjcd*)p)->vcache;
  int i = 0;
  printf("cache info dump:\n");
  while (cptr) {
    printf("%d,%d,%d,%d,%d,%"PRId64",%"PRIlld":%s\n",
	i, cptr->id, cptr->flags, cptr->w, cptr->h, cptr->frame, (long long) cptr->lru, (cptr->b ? "allocated" : "null"));
    i++;
    cptr = cptr->next;
  }
  printf("------------\n");
}

/* vi:set ts=8 sts=2 sw=2: */
