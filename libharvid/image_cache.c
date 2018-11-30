/*
   This file is part of harvid

   Copyright (C) 2013,2014 Robin Gareus <robin@gareus.org>

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

#include "dlog.h"
#include "image_cache.h"

#include <time.h>
#include <assert.h>
#include <pthread.h>

//#define HASH_EMIT_KEYS 3
#define HASH_FUNCTION HASH_SFH
#include "uthash.h"

/* FLAGS */
#define CLF_VALID 1    //< cacheline is valid (has decoded frame) -- not needed, is it?!
#define CLF_INUSE 2    //< currently being served

typedef struct {
  int id;         // file ID from VidMap
  short w;
  short h;
  int fmt;        // image format
  int fmt_opt;     // image format options (e.g jpeg quality)
  int64_t frame;
  int flags;
  int refcnt;     // CLF_INUSE reference count
  time_t lru;     // east recently used time
  //int hitcount  //  -- unused; least-frequently used idea
  uint8_t *b;     //< data buffer pointer
  size_t   s;     //< data buffer size
  UT_hash_handle hh;
} ImageCacheLine;

#define CLKEYLEN (offsetof(ImageCacheLine, flags) - offsetof(ImageCacheLine, id))

/* image cache control */
typedef struct {
  ImageCacheLine *icache;
  int cfg_cachesize;
  pthread_rwlock_t lock;
  int cache_hits;
  int cache_miss;
} ICC;


static void ic_flush_cache (ICC *icc) {
  ImageCacheLine *cl, *tmp;
  pthread_rwlock_wrlock(&icc->lock);

  HASH_ITER(hh, icc->icache, cl, tmp) {
    HASH_DEL(icc->icache, cl);
    free(cl->b);
    free(cl);
  }

  icc->cache_hits = 0;
  icc->cache_miss = 0;
  pthread_rwlock_unlock(&icc->lock);
}

////////////

void icache_create(void **p) {
  ICC *icc;
  (*((ICC**)p)) = (ICC*) calloc(1, sizeof(ICC));
  icc = (*((ICC**)p));
  icc->cfg_cachesize = 32;
  icc->icache = NULL;
  icc->cache_hits = icc->cache_miss = 0;
  pthread_rwlock_init(&icc->lock, NULL);
}

void icache_destroy(void **p) {
  ICC *icc = (*((ICC**)p));
  pthread_rwlock_destroy(&icc->lock);
  free(icc->icache);
  free(*((ICC**)p));
  *p = NULL;
}

void icache_resize(void *p, int size) {
  if (size < ((ICC*)p)->cfg_cachesize)
    ic_flush_cache((ICC*) p);
  if (size > 0)
    ((ICC*)p)->cfg_cachesize = size;
}

void icache_clear (void *p) {
  ic_flush_cache((ICC*) p);
}


uint8_t *icache_get_buffer(void *p, unsigned short id, int64_t frame, int fmt, int fmt_opt, short w, short h, size_t *size, void **cptr) {
  ICC *icc = (ICC*) p;
  ImageCacheLine *cl = NULL;
  const ImageCacheLine cmp = {id, w, h, fmt, fmt_opt, frame, 0, 0, 0, NULL, 0};

  pthread_rwlock_rdlock(&icc->lock);
  HASH_FIND(hh, icc->icache, &cmp, CLKEYLEN, cl);
  pthread_rwlock_unlock(&icc->lock);

  if (cl) {
    pthread_rwlock_wrlock(&icc->lock);
    if (cl->flags&CLF_VALID) {
      cl->refcnt++;
      cl->flags |= CLF_INUSE;
      pthread_rwlock_unlock(&icc->lock);
      if (size) *size = cl->s;
      if (cptr) *cptr = cl;
      cl->lru = time(NULL);
      icc->cache_hits++;
      return cl->b;
    }
    pthread_rwlock_unlock(&icc->lock);
  }

  /* not found in cache */
  icc->cache_miss++;
  if (size) *size = 0;
  if (cptr) *cptr = NULL;
  return NULL;
}

int icache_add_buffer(void *p, unsigned short id, int64_t frame, int fmt, int fmt_opt, short w, short h, uint8_t *buf, size_t size) {
  ICC *icc = (ICC*) p;
  ImageCacheLine *cl = NULL, *tmp;

  pthread_rwlock_rdlock(&icc->lock); // wrlock ?!
  if (HASH_COUNT(icc->icache) >= icc->cfg_cachesize) {
    ImageCacheLine *tmp, *ilru = NULL;
    time_t lru = time(NULL) + 1;

    HASH_ITER(hh, icc->icache, cl, tmp) {
      if (cl->lru < lru && !(cl->flags & CLF_INUSE)) {
        lru = cl->lru;
        ilru = cl;
      }
    }

    if (ilru) {
      HASH_DEL(icc->icache, ilru);
      free(ilru->b);
      cl = ilru;
      memset(cl, 0, sizeof(ImageCacheLine));
    }
  }
  pthread_rwlock_unlock(&icc->lock);

  if (!cl)
    cl = calloc(1, sizeof(ImageCacheLine));

  cl->id = id;
  cl->w = w;
  cl->h = h;
  cl->fmt = fmt;
  cl->fmt_opt = fmt_opt;
  cl->frame = frame;
  cl->lru = 0;
  cl->b = buf;
  cl->s = size;
  cl->flags = CLF_VALID;

  pthread_rwlock_wrlock(&icc->lock);

  // check if found - added almost simultaneously by other thread
  HASH_FIND(hh, icc->icache, cl, CLKEYLEN, tmp);
  if (tmp) {
    pthread_rwlock_unlock(&icc->lock);
    free(cl);
    return -1; // buffer is freed by parent
  }
  HASH_ADD(hh, icc->icache, id, CLKEYLEN, cl);
  pthread_rwlock_unlock(&icc->lock);
  return 0;
}

void icache_release_buffer(void *p, void *cptr) {
  ICC *icc = (ICC*) p;
  if (!cptr) return;
  ImageCacheLine *cl = (ImageCacheLine *)cptr;
  pthread_rwlock_wrlock(&icc->lock);
  if (--cl->refcnt < 1) {
    assert(cl->refcnt >= 0);
    cl->flags &= ~CLF_INUSE;
  }
  pthread_rwlock_unlock(&icc->lock);
}

static char *flags2txt(int f) {
  char *rv = NULL;
  size_t off = 0;

  if (f == 0) {
    rv = (char*) realloc(rv, (off+2) * sizeof(char));
    off += sprintf(rv+off, "-");
    return rv;
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

static const char * fmt_to_text(int fmt) {
  switch (fmt) {
    case 1:
      return "JPEG";
    case 2:
      return "PNG";
    case 3:
      return "PPM";
    default:
      return "?";
  }
}

void icache_info_html(void *p, char **m, size_t *o, size_t *s, int tbl) {
  int i = 1;
  ImageCacheLine *cptr, *tmp;
  uint64_t total_bytes = 0;
  char bsize[32];

  if (tbl&1) {
    rprintf("<h3>Encoded Image Cache:</h3>\n");
    rprintf("<p>max available: %i\n", ((ICC*)p)->cfg_cachesize);
    rprintf("cache-hits: %d, cache-misses: %d</p>\n", ((ICC*)p)->cache_hits, ((ICC*)p)->cache_miss);
    rprintf("<table style=\"text-align:center;width:100%%\">\n");
  } else {
    rprintf("<tr><td colspan=\"8\" class=\"left\"><h3>Encoded Image Cache :</h3></td></tr>\n");
    rprintf("<tr><td colspan=\"8\" class=\"left line\">max available: %d\n", ((ICC*)p)->cfg_cachesize);
    rprintf(", cache-hits: %d, cache-misses: %d</td></tr>\n", ((ICC*)p)->cache_hits, ((ICC*)p)->cache_miss);
  }
  rprintf("<tr><th>#</th><th>file-id</th><th>Flags</th><th>Allocated Bytes</th><th>Geometry</th><th>Buffer</th><th>Frame#</th><th>Last Hit</th></tr>\n");
  /* walk comlete tree */
  pthread_rwlock_rdlock(&((ICC*)p)->lock);
  HASH_ITER(hh, ((ICC*)p)->icache, cptr, tmp) {
    char *tmp = flags2txt(cptr->flags);
#ifdef _WIN32
    rprintf("<tr><td>%d.</td><td>%d</td><td>%s</td><td>%lu bytes</td><td>%dx%d</td>",
        i, cptr->id, tmp, (long unsigned) cptr->s, cptr->w, cptr->h);
#else
    rprintf("<tr><td>%d.</td><td>%d</td><td>%s</td><td>%zu bytes</td><td>%dx%d</td>",
        i, cptr->id, tmp, cptr->s, cptr->w, cptr->h);
#endif

    if (cptr->fmt == 1) {
      rprintf("<td>%s Q:%d</td>", fmt_to_text(cptr->fmt), cptr->fmt_opt);
    } else {
      rprintf("<td>%s</td>", fmt_to_text(cptr->fmt));
    }

    rprintf("<td>%"PRIlld"</td><td>%"PRIlld"</td></tr>\n",
        (long long) cptr->frame, (long long) cptr->lru);

    free(tmp);
    total_bytes += cptr->s;
    i++;
  }

  if ((tbl&1) == 0) {
    rprintf("<tr><td colspan=\"8\" class=\"dline\"></td></tr>\n");
  }

  if (total_bytes < 1024) {
    sprintf(bsize, "%.0f %s", total_bytes / 1.0, "");
  } else if (total_bytes < 1024000 ) {
    sprintf(bsize, "%.1f %s", total_bytes / 1024.0, "Ki");
  } else if (total_bytes < 10485760) {
    sprintf(bsize, "%.1f %s", total_bytes / 1048576.0, "Mi");
  } else if (total_bytes < 1048576000) {
    sprintf(bsize, "%.2f %s", total_bytes / 1048576.0, "Mi");
  } else {
    sprintf(bsize, "%.2f %s", total_bytes / 1073741824.0, "Gi");
  }

  rprintf("<tr><td colspan=\"8\" class=\"left\">cache size: %sB in memory</td></tr>\n", bsize);
  pthread_rwlock_unlock(&((ICC*)p)->lock);
  if (tbl&2) {
    rprintf("</table>\n");
  }
}

// vim:sw=2 sts=2 ts=8 et:
