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
#include "ffdecoder.h"

#include <time.h>
#include <assert.h>
#include <pthread.h>

typedef struct videocacheline {
  struct videocacheline *next; // TODO: if cache grows large we may want to use tree-structure or dbl.linked lists.
  int64_t frame;
  time_t lru;
  int id;
  int flags;
  int w;
  int h;
  uint8_t *b; //* data buffer pointer
} videocacheline;


/* FLAGS */
#define CLF_USED 1 //< currently in use (decoder active, locked)
//#define CLF_NEW 2 //< decoded but but not yet used
//#define CLF_ERR 4 //< error
#define CLF_ALLOC 8 //< ->b is a pointer to some allocated mem.
#define CLF_VALID 16 //< cacheline is valid
//#define CLF_READ 32 //< this cache has been hit at least once after decoding.

#define SPP (4) // samples per pixel

static videocacheline *newcl(videocacheline *cache) {
  videocacheline *cl = calloc(1, sizeof(videocacheline));
  //append new line to cache
  videocacheline *cptr = cache;
  while (cptr && cptr->next) cptr=cptr->next;
  if (cptr) cptr->next=cl;
  return(cl);
}

/* free and clear a cacheline
 * fails if the cache-line is flagged /CLF_USED/
 */
static int freecl(videocacheline *cptr) {
  if (!cptr) return (1);
  if ((cptr->flags&CLF_USED)) return(1);

  if (cptr->flags&CLF_ALLOC) {
    assert(cptr->b);
    free(cptr->b); cptr->b=0;
  }
  cptr->lru=0; cptr->frame=0; cptr->flags=0;
  return (0);
}

static videocacheline *getcl(videocacheline *cache, int cfg_cachesize) {
  int i=0;
  videocacheline *over = NULL; // if cache is full - this one will be flushed and re-used
  time_t lru = time(NULL) + 1;
  videocacheline *cptr = cache;

  while (cptr) {
    if ((cptr->flags&~(CLF_ALLOC)) == 0) return (cptr);
    if (!(cptr->flags&CLF_USED) && (cptr->lru < lru))  {
      lru = cptr->lru;
      over=cptr;
    }
    cptr=cptr->next;
    i++;
  }

  /* TODO optimize; alloc in ulocked-cache. */
  if (i < cfg_cachesize)
    return(newcl(cache));

  cptr=over; // replace LRU
  if (cptr && !(cptr->flags&CLF_USED)) {
    //printf("LRU %d - %lu (frame:%i)\n",i,cptr->lru,cptr->frame);
    // if same w,h - we can keep ALLOC -> call realloccl_buf() after this fn.
    cptr->lru=0; cptr->frame=0;
    cptr->flags&=(CLF_ALLOC);
    return (cptr);
  }

  /* all cache lines are in USE/locked */

  dlog(DLOG_CRIT, "frame-cache, cache full all cache-lines in use\n");
  return (NULL);
}

static videocacheline *testclwh(videocacheline *cache, int64_t frame, int w, int h, int id, int ignoremask) {
  videocacheline *cptr = cache;
  while (cptr) {
    if (cptr->flags&CLF_VALID && cptr->frame == frame
        && ((ignoremask&1)==1 || cptr->w == w)
        && ((ignoremask&2)==2 || cptr->h == h)
        && ((ignoremask&4)==4 || cptr->id == id)
       )
      return(cptr);
    cptr=cptr->next;
  }
  return(NULL);
}

/* clear cache
 * if f==1 force also to flush data (even if it's in USE)
 * if f==0 the cache is flushed but data objects are only freed next
 * time a cacheline is needed
 */
static void clearcache(videocacheline *cache, int f) {
  videocacheline *cptr = cache;
  while (cptr) {
    videocacheline *mem = cptr;
    if (f) cptr->flags&=~CLF_USED;
    assert(!freecl(cptr));
    cptr=cptr->next;
    mem->next=NULL;
    if (f && mem!=cache) free(mem);
  }
  if (f) cache->next=NULL;
}

// TODO change API - (spp) -> use callback to calc. planar&packed data struct size.
// clear VALID flag??
static void realloccl_buf(videocacheline *cptr, int w, int h, int spp) {
  if (cptr->flags&CLF_ALLOC)
    if (cptr->w==w && cptr->h==h)
      return; // already allocated  // TODO: check SPP

  if (cptr->flags&CLF_ALLOC) free(cptr->b);
  cptr->b=NULL;
  cptr->b= calloc(w*h*spp,sizeof(uint8_t)); // TODO ask decoder about this
//cptr->b= realloc(cptr->b,w*h*SPP*sizeof(uint8_t));
  cptr->flags|=CLF_ALLOC;
  cptr->w=w;
  cptr->h=h;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-

typedef struct {
  int cfg_cachesize;
  videocacheline *vcache;
//int render_fmt;
  pthread_mutex_t lock; // TODO - lock to modify vcache itself
  int cache_hits;
  int cache_miss;
} xjcd;

static void fc_initialize_cache (xjcd *cc) {
  assert(!cc->vcache);
  cc->vcache = newcl(NULL);
  cc->cache_hits = 0;
  cc->cache_miss = 0;
//cc->render_fmt = 0;  // TODO set cache's RENDER_FMT.
  pthread_mutex_init(&cc->lock, NULL);
}

static videocacheline *fc_readcl(xjcd *cc, void *dc, int64_t frame, int w, int h, int vid) {
  /* check if the requested frame is cached */
  videocacheline *rv = testclwh(cc->vcache, frame, w, h, vid, 0);
  if (rv) {
    rv->lru=time(NULL);
    cc->cache_hits++;
    return(rv);
  }

  /* too bad, now we need to allocate a cacheline and then decode the video.. */

  int timeout=100;
  do {
    pthread_mutex_lock(&cc->lock);
    rv=getcl(cc->vcache, cc->cfg_cachesize);
    if (rv) {
      rv->flags|=CLF_USED;
    }
    pthread_mutex_unlock(&cc->lock);
    if (!rv) {
      mymsleep(5);
    }
  } while(--timeout > 0 && !rv);

  if (!rv) {
    return (rv);
  }

  realloccl_buf(rv, w, h, SPP); // FIXME ; call ff_get_buffersize()  - use dctrl_get_info(); dctrl_decode(..) wrapper API

  // Fill cacheline with data - decode video
  if (dctrl_decode(dc, vid, frame, rv->b, w, h)) {
    rv->flags&=~CLF_VALID;
    rv->flags&=~CLF_USED;
    printf("Decode failed\n");
    dlog(DLOG_WARNING, "Cache : decode failed.\n");
    return (rv); // XXX
  }

  rv->id=vid;
  rv->frame=frame;
  // rv->w=w; rv->h=h; - done during realloc_buf
  rv->lru=time(NULL);
  rv->flags|=CLF_VALID;
  rv->flags&=~CLF_USED;
  cc->cache_miss++;
  return(rv);
}

// ---------------------------------------------------------------------------

static void fc_flush_cache (xjcd *cc) {
  pthread_mutex_lock(&cc->lock);
  clearcache(cc->vcache,1);
  cc->cache_hits = 0;
  cc->cache_miss = 0;
  pthread_mutex_unlock(&cc->lock);
}

void vcache_clear (void *p) {
  xjcd *cc = (xjcd*) p;
  pthread_mutex_lock(&cc->lock);
  clearcache(cc->vcache,0);
  cc->cache_hits = 0;
  cc->cache_miss = 0;
  pthread_mutex_unlock(&cc->lock);
}

void vcache_create(void **p) {
  (*((xjcd**)p)) = (xjcd*) calloc(1,sizeof(xjcd));
  (*((xjcd**)p))->cfg_cachesize=48;
  fc_initialize_cache((*((xjcd**)p)));
}

void vcache_resize(void **p, int size) {
  if (size < (*((xjcd**)p))->cfg_cachesize)
    fc_flush_cache((*((xjcd**)p)));
  if (size>0)
    (*((xjcd**)p))->cfg_cachesize=size;
}

void vcache_destroy(void **p) {
  xjcd *cc = *(xjcd**) p;
  fc_flush_cache(cc);
  pthread_mutex_destroy(&cc->lock);
  free(cc->vcache);
  free(cc);
  *p=NULL;
}

uint8_t *vcache_get_buffer(void *p, void *dc, int id, int64_t frame, int w, int h) {
  videocacheline *cl = fc_readcl((xjcd*)p, dc, frame, w ,h, id);
  if (!cl) return NULL;
  return cl->b;
}

#if 0
int cache_invalidate_buffer(void *p, int id, unsigned long frame, int w, int h) {
  videocacheline *rv = testclwh(((xjcd*)p)->vcache, frame, w, h, id, 0);
  if (rv) {
    freecl(rv);
    return 0;
  }
  return -1;
}
#endif

static char *flags2txt(int f) {
  char *rv = NULL;
  size_t off =0;

  if (f==0) {
    rv = (char*) realloc(rv, (off+2) * sizeof(char));
    off+=sprintf(rv+off, "-");
    return rv;
  }
  if (f&CLF_USED) {
    rv = (char*) realloc(rv, (off+8) * sizeof(char));
    off+=sprintf(rv+off, "locked ");
  }
  if (f&CLF_ALLOC) {
    rv = (char*) realloc(rv, (off+11) * sizeof(char));
    off+=sprintf(rv+off, "allocated ");
  }
  if (f&CLF_VALID) {
    rv = (char*) realloc(rv, (off+7) * sizeof(char));
    off+=sprintf(rv+off, "valid ");
  }
  return rv;
}

size_t vcache_info_html(void *p, char *m, size_t n) {
  size_t off =0;
  videocacheline *cptr = ((xjcd*)p)->vcache;
  off+=snprintf(m+off, n-off, "<h3>Cache Info:</h3>\n");
  off+=snprintf(m+off, n-off, "<p>Size: max. %i entries.\n", ((xjcd*)p)->cfg_cachesize);
  off+=snprintf(m+off, n-off, "Hits: %d, Misses: %d</p>\n", ((xjcd*)p)->cache_hits, ((xjcd*)p)->cache_miss);
  off+=snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off+=snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Flags</th><th>W</th><th>H</th><th>Frame#</th><th>LRU</th><th>buffer</th></tr>\n");
  int i=0;
  while (cptr) {
    char *tmp = flags2txt(cptr->flags);
    off+=snprintf(m+off, n-off,
        "<tr><td>%d</td><td>%d</td><td>%s</td><td>%d</td><td>%d</td><td>%"PRId64"</td><td>%"PRIlld"</td><td>%s</td></tr>\n",
	i, cptr->id, tmp, cptr->w, cptr->h, cptr->frame, (long long) cptr->lru,(cptr->b?"alloc":"null"));
    free(tmp);
    i++;
    cptr=cptr->next;
  }
  off+=snprintf(m+off, n-off, "</table>\n");
  return(off);
}

void vcache_info_dump(void *p) {
  videocacheline *cptr = ((xjcd*)p)->vcache;
  int i=0;
  printf("cache info dump:\n");
  while (cptr) {
    printf("%d,%d,%d,%d,%d,%"PRId64",%"PRIlld":%s\n",
	i, cptr->id, cptr->flags, cptr->w, cptr->h, cptr->frame, (long long) cptr->lru, (cptr->b?"allocated":"null"));
    i++;
    cptr=cptr->next;
  }
  printf("------------\n");
}

/* vi:set ts=8 sts=2 sw=2: */
