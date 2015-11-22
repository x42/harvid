/*
   This file is part of harvid

   Copyright (C) 2008-2014 Robin Gareus <robin@gareus.org>

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
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include "decoder_ctrl.h"
#include "frame_cache.h"
#include "ffdecoder.h"
#include "ffcompat.h"
#include "dlog.h"

#define DEFAULT_PIX_FMT (AV_PIX_FMT_RGB24) // TODO global default

//#define HASH_EMIT_KEYS 3
#define HASH_FUNCTION HASH_SAX
#include "uthash.h"

/* Video Object Flags */
#define VOF_USED 1    ///< decoder is currently in use - decoder locked
#define VOF_OPEN 2    ///< decoder is idle - decoder is a valid pointer for the file/ID
#define VOF_VALID 4   ///< ID and filename are valid (ID may be in use by cache)
#define VOF_PENDING 8 ///< decoder is just opening a file (my_open_movie)
#define VOF_INFO 16   ///< decoder is currently in use for info (size/fps) lookup only

/* id + fmt */
#define CLKEYLEN (offsetof(JVOBJECT, frame) - offsetof(JVOBJECT, id))

typedef struct JVOBJECT {
  unsigned short id;    // file ID from VidMap
  int fmt;              // pixel format
  int64_t frame;        // decoded frame-number
  time_t lru;           // least recently used time
  int hitcount_decoder; // least-frequently used idea
  int hitcount_info;    // least-frequently used idea
  pthread_mutex_t lock; // lock to modify flags and refcnt
  int flags;
  int infolock_refcnt;
  void *decoder;        // opaque ffdecoder
  struct JVOBJECT *next;
  UT_hash_handle hhi;
  UT_hash_handle hhf;
} /*__attribute__((__packed__)) */ JVOBJECT;

typedef struct VidMap {
  unsigned short id;
  char *fn;
  time_t lru;
  UT_hash_handle hh;
  UT_hash_handle hr;
} VidMap;

typedef struct JVD {
  JVOBJECT *jvo; // list of all decoder objects
  JVOBJECT *jvf; // hash-index of jvo
  JVOBJECT *jvi; // hash-index of jvo
  VidMap *vml;   // filename -> id map
  VidMap *vmr;   // filename <- id map
  unsigned short monotonic; // monotonic count for VidMap ID (wrap-around case is handled)
  int max_objects; // config
  int cache_size;  // config
  int busycnt; // prevent cache purge/cleanup while decoders are active
  int purge_in_progress;
  pthread_mutex_t lock_jvo;  // lock to modify (append to) jvo list (TODO consolidate w/ lock_jdh)
  pthread_rwlock_t lock_jdh; // lock for jvo index-hash
  pthread_rwlock_t lock_vml; // lock to modify monotonic (TODO consolidate w/ lock_jdh)
  pthread_mutex_t lock_busy; // lock to modify busycnt;
} JVD;

///////////////////////////////////////////////////////////////////////////////
// ffdecoder wrappers

static inline int my_decode(void *vd, unsigned long frame, uint8_t *b, int w, int h) {
  int rv;
  ff_resize(vd, w, h, b, NULL);
  rv = ff_render(vd, frame, b, w, h, 0, w, w);
  ff_set_bufferptr(vd, NULL);
  return rv;
}

static inline int my_open_movie(void **vd, char *fn, int render_fmt) {
  if (!fn) {
    dlog(DLOG_ERR, "DCTL: trying to open file w/o filename.\n");
    return -1;
  }
  ff_create(vd);
  assert (
         render_fmt == AV_PIX_FMT_YUV420P
      || render_fmt == AV_PIX_FMT_YUV440P
      || render_fmt == AV_PIX_FMT_YUYV422
      || render_fmt == AV_PIX_FMT_UYVY422
      || render_fmt == AV_PIX_FMT_RGB24
      || render_fmt == AV_PIX_FMT_BGR24
      || render_fmt == AV_PIX_FMT_RGBA
      || render_fmt == AV_PIX_FMT_ARGB
      || render_fmt == AV_PIX_FMT_BGRA
      );

  if (!ff_open_movie (*vd, fn, render_fmt)) {
    debugmsg(DEBUG_DCTL, "DCTL: opened file: '%s'\n", fn);
  } else {
    dlog(DLOG_ERR, "DCTL: Cannot open file: '%s'\n", fn);
    ff_destroy(vd);
    return(1);
  }
  return(0);
}

static inline void my_destroy(void **vd) {
  ff_destroy(vd);
}

static inline void my_get_info(void *vd, VInfo *i) {
  ff_get_info(vd, i);
}

static inline void my_get_info_canonical(void *vd, VInfo *i, int w, int h) {
  ff_get_info_canonical(vd, i, w, h);
}

///////////////////////////////////////////////////////////////////////////////
// Video object management
//

static JVOBJECT *newjvo (JVOBJECT *jvo, pthread_mutex_t *appendlock) {
  debugmsg(DEBUG_DCTL, "DCTL: newjvo() allocated new decoder object\n");
  JVOBJECT *n = calloc(1, sizeof(JVOBJECT));
  n->fmt = AV_PIX_FMT_NONE;
  n->frame = -1;
  pthread_mutex_init(&n->lock, NULL);
  JVOBJECT *cptr = jvo;
  pthread_mutex_lock(appendlock);
  while (cptr && cptr->next) cptr = cptr->next;
  if (cptr) cptr->next = n;
  pthread_mutex_unlock(appendlock);
  return(n);
}

/* return idle decoder-object for given file-id
 * prefer decoders with nearby (lower) frame-number
 *
 * this function is non-blocking (no locking):
 * there is no guarantee that the returned object's state
 * was not changed meanwhile.
 */
static JVOBJECT *testjvd(JVOBJECT *jvo, unsigned short id, int fmt, int64_t frame) {
  JVOBJECT *cptr;
  JVOBJECT *dec_closed = NULL;
  JVOBJECT *dec_open = NULL;
  time_t lru_open = time(NULL) + 1;
  time_t lru_closed = time(NULL) + 1;
  int64_t framediff = -1;
  int found = 0, avail = 0;

  for (cptr = jvo; cptr; cptr = cptr->next) {
    if (!(cptr->flags&VOF_VALID) || cptr->id != id) {
      continue;
    }
    if (fmt != AV_PIX_FMT_NONE && cptr->fmt != fmt
        && cptr->fmt != AV_PIX_FMT_NONE
        ) {
      continue;
    }
    found++;

    if (cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO)) {
      continue;
    }
    avail++;

    if (!(cptr->flags&VOF_OPEN)) {
      if (cptr->lru < lru_closed) {
        lru_closed = cptr->lru;
        dec_closed = cptr;
      }
      continue;
    }

    if (frame < 0) { // LRU only
      if (cptr->lru < lru_open) {
        lru_open = cptr->lru;
        dec_open = cptr;
      }
    } else { // check frame proximity
      int64_t fd = frame - cptr->frame;
      if (framediff < 0 || (fd >= 0 && fd == framediff)) {
        if (cptr->lru < lru_open) {
          lru_open = cptr->lru;
          dec_open = cptr;
          if (fd > 0)
            framediff = fd;
        }
      } else if (fd >= 0 && fd < framediff) {
          lru_open = cptr->lru;
          dec_open = cptr;
          framediff = fd;
      }
    }
  } /* end loop over all decoder objects */

  debugmsg(DEBUG_DCTL, "DCTL: found %d avail. from %d total decoder(s) for file-id:%d. [%s]\n",
      avail, found, id, dec_open?"open":dec_closed?"closed":"N/A");

  if (dec_open) {
    return(dec_open);
  }
  if (dec_closed) {
    return(dec_closed);
  }
  return(NULL);
}

static void hashref_delete_jvo(JVD *jvd, JVOBJECT *jvo) {
  JVOBJECT *jvx, *jvtmp;
  pthread_rwlock_wrlock(&jvd->lock_jdh);
  HASH_ITER(hhf, jvd->jvf, jvx, jvtmp) {
    if (jvx == jvo) {
      debugmsg(DEBUG_DCTL, "delete index hash -> i:%d f:%d\n", jvo->id, jvo->fmt);
      HASH_DELETE(hhi, jvd->jvi, jvo);
      HASH_DELETE(hhf, jvd->jvf, jvo);
      memset(&jvo->hhi, 0, sizeof(UT_hash_handle));
      memset(&jvo->hhf, 0, sizeof(UT_hash_handle));
      break;
    }
  }
  pthread_rwlock_unlock(&jvd->lock_jdh);
}

/* clear object information
 if f==4 force also to flush data (even if it's in USE -- may segfault)
 if f==3 force also to flush data (wait fo it to become unused)
 if f==2 all /unused objects/ are invalidated and freed.
 if f==1 all /unused objects/ are invalidated.
 if f==0 all /unused and open objects/ are closed.

 (f>=2 must not be used during /normal/ operation -- it is exclusive to multithread operation)

 @param id ; filter on id ; -1: all in cache.
*/
static int clearjvo(JVD *jvd, int f, int id, int age, pthread_mutex_t *l) {
  JVOBJECT *cptr = jvd->jvo;
  JVOBJECT *prev = jvd->jvo;
  int total = 0, busy = 0, cleared = 0, freed = 0, count = 0, skipped = 0;
  time_t now;

  if (f > 1) {
    pthread_mutex_lock(&jvd->lock_busy);
#if 0
    if (jvd->purge_in_progress) {
      pthread_mutex_unlock(&jvd->lock_busy);
      return;
    }
#endif
    jvd->purge_in_progress++;
    while (jvd->busycnt > 0) {
      pthread_mutex_unlock(&jvd->lock_busy);
      mymsleep(5);
      pthread_mutex_lock(&jvd->lock_busy);
    }
  }

  pthread_mutex_lock(l);
  now = time(NULL);

  while (cptr) {
    JVOBJECT *mem = cptr;
    total++;
    if (id > 0 && cptr->id != id) {
      prev = cptr; cptr = cptr->next;
      skipped++;
      continue;
    }
    if (age > 0 && cptr->lru + age > now) {
      prev = cptr; cptr = cptr->next;
      skipped++;
      continue;
    }

    count++;

    pthread_mutex_lock(&cptr->lock);
    if (cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO)) {
      if (f < 3) {
	pthread_mutex_unlock(&cptr->lock);
	busy++;
        prev = cptr; cptr = cptr->next;
	continue;
      }
      if (f < 4) {
	dlog(DLOG_WARNING, "DTCL: waiting for decoder to be unlocked.\n");
        do {
          pthread_mutex_unlock(&cptr->lock);
          mymsleep(5);
          pthread_mutex_lock(&cptr->lock);
        } while (cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO));
      } else {
        /* we really should not do this */
        dlog(DLOG_ERR, "DCTL: request to free an active decoder.\n");
        cptr->flags &= ~(VOF_USED|VOF_PENDING|VOF_INFO);
        cptr->infolock_refcnt = 0; // XXX may trigger assert() in dctrl_release_infolock()
      }
    }

    if (cptr->flags&VOF_OPEN) {
      my_destroy(&cptr->decoder);
      cptr->decoder = NULL;
      cptr->flags &= ~VOF_OPEN;
      cptr->fmt = AV_PIX_FMT_NONE;
    }

    hashref_delete_jvo(jvd, cptr);

    if (f > 0) {
      cptr->id = 0;
      cptr->lru = 0;
      cptr->hitcount_info = 0;
      cptr->hitcount_decoder = 0;
      cptr->frame = -1;
      cptr->flags &= ~VOF_VALID;
    }

    pthread_mutex_unlock(&cptr->lock);

    cptr = cptr->next;
    if (f > 1 && mem != jvd->jvo) {
      assert (prev != mem);
      prev->next = cptr;
      pthread_mutex_destroy(&mem->lock);
      free(mem);
      freed++;
    } else {
      prev = mem;
      cleared++;
    }
  }
  pthread_mutex_unlock(l);

  if (f > 1) {
    jvd->purge_in_progress--;
    pthread_mutex_unlock(&jvd->lock_busy);
  }

  debugmsg(DEBUG_DCTL, "DCTL: GC processed %d (freed: %d, cleared: %d, busy: %d) skipped: %d, total: %d\n", count, freed, cleared, busy, skipped, total);
  return (cleared);
}


//get some unused allocated jvo or create one.
static JVOBJECT *getjvo(JVD *jvd) {
  int cnt_total = 0;
  JVOBJECT *dec_closed = NULL;
  JVOBJECT *dec_open = NULL;
  time_t lru = time(NULL) + 1;
  JVOBJECT *cptr = jvd->jvo;
#if 1 // garbage collect, close decoders not used since > 10 mins
  clearjvo(jvd, 1, -1, 600, &jvd->lock_jvo);
#endif
  while (cptr) {
    if ((cptr->flags&(VOF_USED|VOF_OPEN|VOF_VALID|VOF_PENDING|VOF_INFO)) == 0) {
      return (cptr);
    }

    if (!(cptr->flags&(VOF_USED|VOF_OPEN|VOF_PENDING|VOF_INFO)) && (cptr->lru < lru)) {
      lru = cptr->lru;
      dec_closed = cptr;
    } else if (!(cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO)) && (cptr->lru < lru)) {
      lru = cptr->lru;
      dec_open = cptr;
    }
    cptr = cptr->next;
    cnt_total++;
  }

  if (dec_closed) {
    cptr = dec_closed;
  } else if (dec_open) {
    cptr = dec_open;
  }
  debugmsg(DEBUG_DCTL, "DCTL: %d/%d decoders; avail: closed: %s open: %s\n",
      cnt_total, jvd->max_objects, dec_closed?"Y":"N", dec_open?"Y":"N");

  // TODO prefer to allocate a new decoder object IFF
  // decoder for same file exists but with different format.
  if (cnt_total < 4
      && cnt_total < jvd->max_objects)
    return(newjvo(jvd->jvo, &jvd->lock_jvo));

  if (cptr && !pthread_mutex_trylock(&cptr->lock)) {
      if (!(cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO))) {

        if (cptr->flags&(VOF_OPEN)) {
          my_destroy(&cptr->decoder); // close it.
          cptr->decoder = NULL; // not really need..
          cptr->fmt = AV_PIX_FMT_NONE;
        }

        hashref_delete_jvo(jvd, cptr);

        cptr->id = 0;
        cptr->lru = 0;
        cptr->flags = 0;
        cptr->frame = -1;
        cptr->hitcount_info = 0;
        cptr->hitcount_decoder = 0;
        assert(cptr->infolock_refcnt == 0);
        pthread_mutex_unlock(&cptr->lock);
        return (cptr);
    }
    pthread_mutex_unlock(&cptr->lock);
  }

  if (cnt_total < jvd->max_objects)
    return(newjvo(jvd->jvo, &jvd->lock_jvo));
  return (NULL);
}

///////////////////////////////////////////////////////////////////////////////
// Video decoder management
//

static void clearvid(JVD* jvd, void *vc) {
  VidMap *vm, *tmp;
  pthread_rwlock_wrlock(&jvd->lock_vml);
  HASH_ITER(hh, jvd->vml, vm, tmp) {
    HASH_DEL(jvd->vml, vm);
    HASH_DELETE(hr, jvd->vmr, vm);
    if (vc) vcache_clear(vc, vm->id);
    clearjvo(jvd, 3, vm->id, -1, &jvd->lock_jvo);
    free(vm->fn);
    free(vm);
  }
  pthread_rwlock_unlock(&jvd->lock_vml);
}

static unsigned short get_id(JVD *jvd, const char *fn, void *vc) {
  VidMap *vm = NULL;
  int rv;

  pthread_rwlock_rdlock(&jvd->lock_vml);
  HASH_FIND_STR(jvd->vml, fn, vm);

  if (vm) {
    rv = vm->id;
    vm->lru = time(NULL);
    pthread_rwlock_unlock(&jvd->lock_vml);
    return rv;
  }
  pthread_rwlock_unlock(&jvd->lock_vml);

  /* create a new entry */
  pthread_rwlock_wrlock(&jvd->lock_vml);

  /* check that it has not been added meanwhile */
  HASH_FIND_STR(jvd->vml, fn, vm);
  if (vm) {
    rv = vm->id;
    vm->lru = time(NULL);
    pthread_rwlock_unlock(&jvd->lock_vml);
    return rv;
  }

  if(HASH_COUNT(jvd->vml) >= jvd->cache_size) {
    /* delete lru */
    VidMap *tmp, *vlru = NULL;
    time_t lru = time(NULL) + 1;
    HASH_ITER(hh, jvd->vml, vm, tmp) {
      if (vm->lru < lru) {
        lru = vm->lru;
        vlru = vm;
      }
    }
    if (vlru) {
      HASH_DEL(jvd->vml, vlru);
      HASH_DELETE(hr, jvd->vmr, vlru);
      free(vlru->fn);
      vm = vlru;
      memset(vm, 0, sizeof(VidMap));
    }
  }

  if (!vm) vm = calloc(1, sizeof(VidMap));

  vm->id = jvd->monotonic++;
  if (jvd->monotonic == 0) {
    dlog(LOG_INFO, "monotonic ID counter roll-over\n");
    jvd->monotonic = 1;
  }
  vm->fn = strdup(fn);
  vm->lru = time(NULL);
  rv = vm->id;
  HASH_ADD_KEYPTR(hh, jvd->vml, vm->fn, strlen(vm->fn), vm);
  HASH_ADD(hr, jvd->vmr, id, sizeof(unsigned short), vm);
  /* invalidate frame-cache for this ID*/
  if (vc) vcache_clear(vc, vm->id);
  pthread_rwlock_unlock(&jvd->lock_vml);
  return rv;
}

static char *get_fn(JVD *jvd, unsigned short id) {
  VidMap *vm;
  pthread_rwlock_rdlock(&jvd->lock_vml);
  HASH_FIND(hr, jvd->vmr, &id, sizeof(unsigned short), vm);
  pthread_rwlock_unlock(&jvd->lock_vml);
  if (vm) return vm->fn; // XXX strdup before unlock
  return NULL;
}

static void release_id(JVD *jvd, unsigned short id) {
  VidMap *vm;
  pthread_rwlock_wrlock(&jvd->lock_vml);
  HASH_FIND(hr, jvd->vmr, &id, sizeof(unsigned short), vm);

  if (vm) {
    HASH_DEL(jvd->vml, vm);
    HASH_DELETE(hr, jvd->vmr, vm);
    pthread_rwlock_unlock(&jvd->lock_vml);
    free(vm->fn);
    free(vm);
    return;
  }
  pthread_rwlock_unlock(&jvd->lock_vml);
  dlog(LOG_ERR, "failed to delete ID from hash table\n");
}


static JVOBJECT *new_video_object(JVD *jvd, unsigned short id, int fmt) {
  JVOBJECT *jvo, *jvx;
  debugmsg(DEBUG_DCTL, "new_video_object()\n");
  do {
    jvo = getjvo(jvd);
    if (!jvo) {
      mymsleep(5);
      sched_yield();
      return NULL;
    }
    if (pthread_mutex_trylock(&jvo->lock)) {
      continue;
    }
    if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID|VOF_PENDING|VOF_INFO))) {
      pthread_mutex_unlock(&jvo->lock);
      continue;
    }
  } while (!jvo);


  jvo->id = id;
  jvo->fmt = fmt == AV_PIX_FMT_NONE ? DEFAULT_PIX_FMT : fmt;
  jvo->frame = -1;
  jvo->flags |= VOF_VALID;

  pthread_rwlock_wrlock(&jvd->lock_jdh);
  HASH_FIND(hhi, jvd->jvi, &id, sizeof(unsigned short), jvx);
  if (!jvx) {
    debugmsg(DEBUG_DCTL, "linking index hash -> i:%d f:%d\n", id, fmt);
    HASH_ADD(hhi, jvd->jvi, id, sizeof(unsigned short), jvo);
    HASH_ADD(hhf, jvd->jvf, id, CLKEYLEN, jvo);
  }
  pthread_rwlock_unlock(&jvd->lock_jdh);

  pthread_mutex_unlock(&jvo->lock);
  return(jvo);
}

#if 0 // unused
/**
 * use-case: re-open a file. - flush decoders (and indirectly cache -> new ID)
 */
static int release_video_object(JVD *jvd, char *fn) {
  int id;
  if ((id = get_id(jvd, fn)) > 0)
    return (clearjvo(jvd, 1, id)); // '0': would close only close and re-use the same ID (cache)
  return(-1);
}
#endif


///////////////////////////////////////////////////////////////////////////////
// part 2a - video decoder control

#define BUSYDEC(jvd) \
  pthread_mutex_lock(&jvd->lock_busy); \
  jvd->busycnt--; \
  pthread_mutex_unlock(&jvd->lock_busy); \

#define BUSYADD(jvd) \
  while (jvd->purge_in_progress) mymsleep(5); \
  pthread_mutex_lock(&jvd->lock_busy); \
  jvd->busycnt++; \
  pthread_mutex_unlock(&jvd->lock_busy); \


// lookup or create new decoder for file ID
static void * dctrl_get_decoder(void *p, unsigned short id, int fmt, int64_t frame, int *err) {
  JVD *jvd = (JVD*)p;
  JVOBJECT *jvo = NULL;
  *err = 0;
  BUSYADD(jvd)

  /* create hash for info lookups, reference first decoder for each id
   * use it IFF frame == -1  (ie. non-blocking info lookups) */
  if (frame < 0) {
    pthread_rwlock_rdlock(&jvd->lock_jdh);
    if (fmt == AV_PIX_FMT_NONE) {
      HASH_FIND(hhi, jvd->jvi, &id, sizeof(unsigned short), jvo);
    } else {
      const JVOBJECT jvt = {id, fmt, 0};
      HASH_FIND(hhf, jvd->jvf, &jvt, CLKEYLEN, jvo);
    }
    pthread_rwlock_unlock(&jvd->lock_jdh);
    if (jvo) {
      debugmsg(DEBUG_DCTL, "ID found in hashtable\n");
      pthread_mutex_lock(&jvo->lock);
      if ((jvo->flags&(VOF_OPEN|VOF_VALID|VOF_PENDING)) == (VOF_VALID|VOF_OPEN)) {
        jvo->infolock_refcnt++;
        jvo->flags |= VOF_INFO;
        pthread_mutex_unlock(&jvo->lock);
        BUSYDEC(jvd)
        return(jvo);
      }
      pthread_mutex_unlock(&jvo->lock);
    }
  }

  while (1) {
    debugmsg(DEBUG_DCTL, "DCTL: get_decoder fileid=%i\n", id);

    if (!jvo) {
      int timeout = 40; // new_video_object() delays 5ms at a time.
      do {
        jvo = testjvd(jvd->jvo, id, fmt, frame);
        if (!jvo) jvo = new_video_object(jvd, id, fmt);
      } while (--timeout > 0 && !jvo);
    }

    if (!jvo) {
      dlog(DLOG_ERR, "DCTL: no decoder object available.\n");
      BUSYDEC(jvd)
      *err = 503; // try again
      return(NULL);
    }

    pthread_mutex_lock(&jvo->lock);
    if ((jvo->flags&(VOF_PENDING))) {
      pthread_mutex_unlock(&jvo->lock);
      jvo = NULL;
      continue;
    }

    if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID|VOF_INFO|VOF_PENDING)) == (VOF_VALID)) {
      jvo->flags |= VOF_PENDING;
      jvo->lru = time(NULL);
      pthread_mutex_unlock(&jvo->lock);

      if (fmt == AV_PIX_FMT_NONE) fmt = DEFAULT_PIX_FMT;

      if (!my_open_movie(&jvo->decoder, get_fn(jvd, jvo->id), fmt)) {
        pthread_mutex_lock(&jvo->lock);
        jvo->fmt = fmt;
        jvo->flags |= VOF_OPEN;
        jvo->flags &= ~VOF_PENDING;
      } else {
        pthread_mutex_lock(&jvo->lock);
        jvo->flags &= ~VOF_PENDING;
        assert(!jvo->decoder);
        pthread_mutex_unlock(&jvo->lock);
        release_id(jvd, jvo->id); // mark ID as invalid
        dlog(DLOG_ERR, "DCTL: opening of movie file failed.\n");
        BUSYDEC(jvd)
        *err = 500; // failed to open format/codec
        return(NULL); // XXX -> 500/inval
      }
    }

    if (frame < 0) {
      /* we only need info -> decoder may be in use */
      if ((jvo->flags&(VOF_OPEN|VOF_VALID)) == (VOF_VALID|VOF_OPEN)) {
        jvo->infolock_refcnt++;
        jvo->flags |= VOF_INFO;
        pthread_mutex_unlock(&jvo->lock);
        BUSYDEC(jvd)
        return(jvo);
      }
    } else {
      if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID)) == (VOF_VALID|VOF_OPEN)) {
        jvo->flags |= VOF_USED;
        pthread_mutex_unlock(&jvo->lock);
        BUSYDEC(jvd)
        return(jvo);
      }
    }

    pthread_mutex_unlock(&jvo->lock);
    debugmsg(DEBUG_DCTL, "DCTL: decoder object was busy.\n");
  }
}

static void dctrl_release_decoder(void *dec) {
  JVOBJECT *jvo = (JVOBJECT *) dec;
  pthread_mutex_lock(&jvo->lock);
  jvo->flags &= ~VOF_USED;
  pthread_mutex_unlock(&jvo->lock);
}

static void dctrl_release_infolock(void *dec) {
  JVOBJECT *jvo = (JVOBJECT *) dec;
  pthread_mutex_lock(&jvo->lock);
  if (--jvo->infolock_refcnt < 1) {
    assert(jvo->infolock_refcnt >= 0);
    jvo->flags &= ~(VOF_INFO);
  }
  pthread_mutex_unlock(&jvo->lock);
}

static inline int xdctrl_decode(void *dec, int64_t frame, uint8_t *b, int w, int h) {
  JVOBJECT *jvo = (JVOBJECT *) dec;
  jvo->lru = time(NULL);
  jvo->hitcount_decoder++;
  int rv = my_decode(jvo->decoder, frame, b, w, h);
  jvo->frame = frame;
  return rv;
}

///////////////////////////////////////////////////////////////////////////////
// part 2b - video object/decoder API - public API

void dctrl_create(void **p, int max_decoders, int cache_size) {
  JVD *jvd;
  (*((JVD**)p)) = (JVD*) calloc(1, sizeof(JVD));
  jvd = (*((JVD**)p));
  jvd->monotonic = 1;
  jvd->max_objects = max_decoders;
  jvd->cache_size = cache_size;

  pthread_mutex_init(&jvd->lock_busy, NULL);
  pthread_mutex_init(&jvd->lock_jvo, NULL);
  pthread_rwlock_init(&jvd->lock_vml, NULL);
  pthread_rwlock_init(&jvd->lock_jdh, NULL);

  jvd->vml = NULL;
  jvd->vmr = NULL;
  jvd->jvi = NULL;
  jvd->jvf = NULL;
  jvd->jvo = newjvo(NULL, &jvd->lock_jvo);

  HASH_ADD(hhi, jvd->jvi, id, sizeof(unsigned short), jvd->jvo);
  HASH_ADD(hhf, jvd->jvf, id, CLKEYLEN, jvd->jvo);
}

void dctrl_destroy(void **p) {
  JVD *jvd = (*((JVD**)p));
  clearjvo(jvd, 3, -1, -1, &jvd->lock_jvo);
  clearvid(jvd, NULL);
  pthread_mutex_destroy(&jvd->lock_busy);
  pthread_mutex_destroy(&jvd->lock_jvo);
  pthread_rwlock_destroy(&jvd->lock_vml);
  pthread_rwlock_destroy(&jvd->lock_jdh);
  free(jvd->jvo);
  free(*((JVD**)p));
  *p = NULL;
}

unsigned short dctrl_get_id(void *vc, void *p, const char *fn) {
  JVD *jvd = (JVD*)p;
  return get_id(jvd, fn, vc);
}


int dctrl_decode(void *p, unsigned short id, int64_t frame, uint8_t *b, int w, int h, int fmt) {
  int err = 0;
  void *dec = dctrl_get_decoder(p, id, fmt, frame, &err);
  if (!dec) {
    dlog(DLOG_WARNING, "DCTL: no decoder available.\n");
    return err;
  }
  int rv = xdctrl_decode(dec, frame, b, w, h);
  dctrl_release_decoder(dec);
  return (rv);
}

int dctrl_get_info(void *p, unsigned short id, VInfo *i) {
  int err = 0;
  JVOBJECT *jvo = (JVOBJECT*) dctrl_get_decoder(p, id, AV_PIX_FMT_NONE, -1, &err);
  if (!jvo) return err;
  my_get_info(jvo->decoder, i);
  jvo->hitcount_info++;
  dctrl_release_infolock(jvo);
  return(0);
}

int dctrl_get_info_scale(void *p, unsigned short id, VInfo *i, int w, int h, int fmt) {
  int err = 0;
  JVOBJECT *jvo = (JVOBJECT*) dctrl_get_decoder(p, id, fmt, -1, &err);
  if (!jvo) return err;
  my_get_info_canonical(jvo->decoder, i, w, h);
  jvo->hitcount_info++;
  dctrl_release_infolock(jvo);
  return(0);
}

void dctrl_cache_clear(void *vc, void *p, int f, int id) {
  JVD *jvd = (JVD*)p;
  clearjvo(jvd, f, id, -1, &jvd->lock_jvo);
  if (vc) clearvid(jvd, vc);

}

///////////////////////////////////////////////////////////////////////////////
// 2c - video object/decoder stats - public API

static char *flags2txt(int f) {
  char *rv = NULL;
  size_t off = 0;

  if (f == 0) {
    rv = (char*) realloc(rv, (off+2) * sizeof(char));
    off += sprintf(rv+off, "-");
    return rv;
  }
  if (f&VOF_USED) {
    rv = (char*) realloc(rv, (off+6) * sizeof(char));
    off += sprintf(rv+off, "used ");
  }
  if (f&VOF_OPEN) {
    rv = (char*) realloc(rv, (off+6) * sizeof(char));
    off += sprintf(rv+off, "open ");
  }
  if (f&VOF_VALID) {
    rv = (char*) realloc(rv, (off+7) * sizeof(char));
    off += sprintf(rv+off, "hasID ");
  }
  if (f&VOF_PENDING) {
    rv = (char*) realloc(rv, (off+9) * sizeof(char));
    off += sprintf(rv+off, "pending ");
  }
  if (f&VOF_INFO) {
    rv = (char*) realloc(rv, (off+9) * sizeof(char));
    off += sprintf(rv+off, "info ");
  }
  return rv;
}

void dctrl_info_html (void *p, char **m, size_t *o, size_t *s, int tbl) {
  JVOBJECT *cptr = ((JVD*)p)->jvo;
  int i = 1;

  VidMap *vm, *tmp;
  if (tbl&1) {
    rprintf("<h3>File/ID Mapping:</h3>\n");
    rprintf("<p>max available: %d</p>\n", ((JVD*)p)->cache_size);
    rprintf("<table style=\"text-align:center;width:100%%\">\n");
  } else {
    if (tbl&2) {
      rprintf("<table style=\"text-align:center;width:100%%\">\n");
    }
    rprintf("<tr><td colspan=\"8\" class=\"left\"><h3>File Mapping:</h3></td></tr>\n");
    rprintf("<tr><td colspan=\"8\" class=\"left line\">max available: %d</td></tr>\n", ((JVD*)p)->cache_size);
  }
  rprintf("<tr><th>#</th><th>file-id</th><th></th><th>Filename</th><th></th><th></th><th></th><th>LRU</th></tr>\n");
  rprintf("\n");
  pthread_rwlock_rdlock(&((JVD*)p)->lock_vml);
  HASH_ITER(hh, ((JVD*)p)->vml, vm, tmp) {
    rprintf("<tr><td>%d.</td><td>%i</td><td></td><td colspan=\"4\" class=\"left\">%s</td><td>%"PRIlld"</td></tr>\n",
        i, vm->id, vm->fn?vm->fn:"(null)", (long long)vm->lru);
    i++;
  }
  pthread_rwlock_unlock(&((JVD*)p)->lock_vml);
  if (tbl&4) {
    rprintf("</table>\n");
  } else {
    rprintf("<tr><td colspan=\"8\" class=\"dline\"></td></tr>\n");
  }

  i = 1;
  if(tbl&4) {
    rprintf("<h3>Decoder Objects:</h3>\n");
    rprintf("<p>max available: %d, busy: %d%s</p>\n", ((JVD*)p)->max_objects, ((JVD*)p)->busycnt, ((JVD*)p)->purge_in_progress?" (purge queued)":"");
    rprintf("<table style=\"text-align:center;width:100%%\">\n");
  } else {
    rprintf("<tr><td colspan=\"8\" class=\"left\"><h3>Decoder Objects:</h3></td></tr>\n");
    rprintf("<tr><td colspan=\"8\" class=\"left line\">max available: %d, busy: %d%s</td></tr>\n",
        ((JVD*)p)->max_objects, ((JVD*)p)->busycnt, ((JVD*)p)->purge_in_progress?" (purge queued)":"");
  }
  rprintf("<tr><th>#</th><th>file-id</th><th>Flags</th><th>Filename</th><th>Hitcount</th><th>PixFmt</th><th>Frame#</th><th>LRU</th></tr>\n");
  rprintf("\n");
  pthread_rwlock_rdlock(&((JVD*)p)->lock_jdh);
  while (cptr) {
    char *tmp, *fn;
    if (cptr->id == 0) {
      cptr = cptr->next;
      continue; // don't list unused root-node.
    }
    tmp = flags2txt(cptr->flags);
    fn = (cptr->flags&VOF_VALID) ? get_fn((JVD*)p, cptr->id) : NULL;
    rprintf(
        "<tr><td>%d.</td><td>%i</td><td>%s</td><td class=\"left\">%s</td><td>i:%d,d:%d</td><td>%s</td><td>%"PRIlld"</td><td>%"PRIlld"</td></tr>\n",
        i, cptr->id, tmp, fn?fn:"-", /* (cptr->decoder?LIBAVCODEC_IDENT:"null"), */
        cptr->hitcount_info, cptr->hitcount_decoder,
        ff_fmt_to_text(cptr->fmt),
        (long long) cptr->frame, (long long)cptr->lru);
    free(tmp);
    cptr = cptr->next;
    i++;
  }
  pthread_rwlock_unlock(&((JVD*)p)->lock_jdh);
  if(tbl&8) {
    rprintf("</table>\n");
  } else {
    rprintf("<tr><td colspan=\"8\" class=\"dline\"></td></tr>\n");
  }
}

// vim:sw=2 sts=2 ts=8 et:
