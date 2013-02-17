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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include "decoder_ctrl.h"
#include "ffdecoder.h"
#include "ffcompat.h"
#include "daemon_log.h"

#define HASH_EMIT_KEYS 3
#define HASH_FUNCTION HASH_SAX
#include "uthash.h"

/* Video Object Flags */
#define VOF_USED 1    ///< decoder is currently in use - decoder locked
#define VOF_OPEN 2    ///< decoder is idle - decoder is a valid pointer for the file/ID
#define VOF_VALID 4   ///< ID and filename are valid (ID may be in use by cache)
#define VOF_PENDING 8 ///< decoder is just opening a file (my_open_movie)
#define VOF_INFO 16   ///< decoder is currently in use for info (size/fps) lookup only

typedef struct JVOBJECT {
  struct JVOBJECT *next;
  void *decoder;        // opaque ffdecoder
  unsigned short id;    // file ID from VidMap
  int64_t frame;        // decoded frame-number
  int fmt;              // pixel format
  time_t lru;           // least recently used time
  //int hitcount        //  -- unused; least-frequently used idea
  pthread_mutex_t lock; // lock to modify flags and refcnt
  int flags;
  int infolock_refcnt;
} JVOBJECT;

typedef struct VidMap {
  unsigned short id;
  char *fn;
  time_t lru;
  UT_hash_handle hh;
  UT_hash_handle hr;
} VidMap;

typedef struct JVD {
  JVOBJECT *jvo; // list of all decoder objects
  VidMap *vml;   // filename -> id map
  VidMap *vmr;   // filename <- id map
  unsigned short monotonic;
  int max_objects;
  int cache_size;
  int busycnt;
  int purge_in_progress;
  pthread_mutex_t lock_jvo;  // lock to modify (append to) jvo list
  pthread_rwlock_t lock_vml; // lock to modify VidMap (inc monotonic)
  pthread_mutex_t lock_busy; // lock to read/increment monotonic;
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
         render_fmt == PIX_FMT_YUV420P
      || render_fmt == PIX_FMT_YUV440P
      || render_fmt == PIX_FMT_YUYV422
      || render_fmt == PIX_FMT_RGB24
      || render_fmt == PIX_FMT_BGR24
      || render_fmt == PIX_FMT_RGBA
      || render_fmt == PIX_FMT_ARGB
      || render_fmt == PIX_FMT_BGRA
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
  n->fmt = PIX_FMT_NONE;
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
 * had not changed meanwhile.
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
    if (fmt != PIX_FMT_NONE && cptr->fmt != fmt
        && cptr->fmt != PIX_FMT_NONE
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
      cptr->fmt = PIX_FMT_NONE;
    }

    if (f > 0) {
      cptr->id = 0;
      cptr->lru = 0;
      cptr->frame = -1;
      cptr->flags &= ~VOF_VALID;
    }

    pthread_mutex_unlock(&cptr->lock);

    cptr = cptr->next;
    if (f > 1 && mem != jvd->jvo) {
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

  dlog(LOG_INFO, "DCTL: GC processed %d (freed: %d, cleared: %d, busy: %d) skipped: %d, total: %d\n", count, freed, cleared, busy, skipped, total);
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
          cptr->fmt = PIX_FMT_NONE;
        }

        cptr->id = 0;
        cptr->lru = 0;
        cptr->flags = 0;
        cptr->frame = -1;
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

static void clearvid(JVD* jvd) {
  VidMap *vm, *tmp;
  pthread_rwlock_wrlock(&jvd->lock_vml);
  HASH_ITER(hh, jvd->vml, vm, tmp) {
    HASH_DEL(jvd->vml,vm);
    HASH_DELETE(hr, jvd->vmr, vm);
    free(vm->fn);
    free(vm);
  }
  pthread_rwlock_unlock(&jvd->lock_vml);
}

static unsigned short get_id(JVD *jvd, const char *fn) {
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
  vm->fn = strdup(fn);
  vm->lru = time(NULL);
  rv = vm->id;
  HASH_ADD_KEYPTR(hh, jvd->vml, vm->fn, strlen(vm->fn), vm);
  HASH_ADD(hr, jvd->vmr, id, sizeof(unsigned short), vm);
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


static JVOBJECT *new_video_object(JVD *jvd, unsigned short id) {
  JVOBJECT *jvo;
  debugmsg(DEBUG_DCTL, "new_video_object()\n");
  do {
    jvo = getjvo(jvd);
    if (!jvo) {
      mymsleep(5);
      sched_yield();
      return NULL;
    }
    if (pthread_mutex_trylock(&jvo->lock))
      continue;
    if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID|VOF_PENDING|VOF_INFO))) {
      pthread_mutex_unlock(&jvo->lock);
      continue;
    }
  } while (!jvo);

  jvo->id = id;
  jvo->frame = -1;
  jvo->fmt = PIX_FMT_NONE;
  jvo->flags |= VOF_VALID;
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
static void * dctrl_get_decoder(void *p, unsigned short id, int fmt, int64_t frame) {
  JVD *jvd = (JVD*)p;
  BUSYADD(jvd)

tryagain:
  debugmsg(DEBUG_DCTL, "DCTL: get_decoder fileid=%i\n", id);

  JVOBJECT *jvo;
  int timeout = 40; // new_video_object() delays 5ms at a time.
  do {
    jvo = testjvd(jvd->jvo, id, fmt, frame);
    if (!jvo) jvo = new_video_object(jvd, id);
  } while (--timeout > 0 && !jvo);

  if (!jvo) {
    dlog(DLOG_ERR, "DCTL: no decoder object available.\n");
    BUSYDEC(jvd)
    return(NULL);
  }

  pthread_mutex_lock(&jvo->lock);
  if ((jvo->flags&(VOF_PENDING))) {
    pthread_mutex_unlock(&jvo->lock);
    goto tryagain;
  }
  jvo->flags |= VOF_PENDING;
  pthread_mutex_unlock(&jvo->lock);

  if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID|VOF_INFO)) == (VOF_VALID)) {
    if (fmt == PIX_FMT_NONE) fmt = PIX_FMT_RGB24; // TODO global default
    if (!my_open_movie(&jvo->decoder, get_fn(jvd, jvo->id), fmt)) {
      pthread_mutex_lock(&jvo->lock);
      jvo->fmt = fmt;
      jvo->flags |= VOF_OPEN;
      jvo->flags &= ~VOF_PENDING;
      pthread_mutex_unlock(&jvo->lock);
    } else {
      pthread_mutex_lock(&jvo->lock);
      jvo->flags &= ~VOF_PENDING;
      assert(jvo->fmt == PIX_FMT_NONE);
      assert(!jvo->decoder);
      pthread_mutex_unlock(&jvo->lock);
      release_id(jvd, jvo->id); // mark ID as invalid
      dlog(DLOG_ERR, "DCTL: opening of movie file failed.\n");
      BUSYDEC(jvd)
      return(NULL);
    }
  }

  pthread_mutex_lock(&jvo->lock);
  jvo->flags &= ~VOF_PENDING;
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

  dlog(DLOG_WARNING, "DCTL: decoder object was busy.\n");
  goto tryagain;

  BUSYDEC(jvd)
  return(NULL);
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

  jvd->vml = NULL;
  jvd->vmr = NULL;
  jvd->jvo = newjvo(NULL, &jvd->lock_jvo);
}

void dctrl_destroy(void **p) {
  JVD *jvd = (*((JVD**)p));
  clearjvo(jvd, 3, -1, -1, &jvd->lock_jvo);
  clearvid(jvd);
  pthread_mutex_destroy(&jvd->lock_busy);
  pthread_mutex_destroy(&jvd->lock_jvo);
  pthread_rwlock_destroy(&jvd->lock_vml);
  free(jvd->jvo);
  free(*((JVD**)p));
  *p = NULL;
}

unsigned short dctrl_get_id(void *p, const char *fn) {
  JVD *jvd = (JVD*)p;
  return get_id(jvd, fn);
}


int dctrl_decode(void *p, unsigned short id, int64_t frame, uint8_t *b, int w, int h, int fmt) {
  void *dec = dctrl_get_decoder(p, id, fmt, frame);
  if (!dec) {
    dlog(DLOG_WARNING, "DCTL: no decoder available.\n");
    return -1;
  }
  int rv = xdctrl_decode(dec, frame, b, w, h);
  dctrl_release_decoder(dec);
  return (rv);
}

int dctrl_get_info(void *p, unsigned short id, VInfo *i) {
  JVOBJECT *jvo = (JVOBJECT*) dctrl_get_decoder(p, id, PIX_FMT_NONE, -1);
  if (!jvo) return -1;
  my_get_info(jvo->decoder, i);
  dctrl_release_infolock(jvo);
  return(0);
}

int dctrl_get_info_scale(void *p, unsigned short id, VInfo *i, int w, int h, int fmt) {
  JVOBJECT *jvo = (JVOBJECT*) dctrl_get_decoder(p, id, fmt, -1);
  if (!jvo) return -1;
  my_get_info_canonical(jvo->decoder, i, w, h);
  dctrl_release_infolock(jvo);
  return(0);
}

void dctrl_cache_clear(void *p, int f, unsigned short id) {
  JVD *jvd = (JVD*)p;
  clearjvo(jvd, f, id, -1, &jvd->lock_jvo);
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

size_t dctrl_info_html (void *p, char *m, size_t n) {
  JVOBJECT *cptr = ((JVD*)p)->jvo;
  int i = 0;
  size_t off = 0;

  VidMap *vm, *tmp;
  pthread_rwlock_rdlock(&((JVD*)p)->lock_vml);
  off += snprintf(m+off, n-off, "<h3>File Mapping:</h3>\n");
  off += snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off += snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Filename</th><th>LRU</th></tr>\n");
  off += snprintf(m+off, n-off, "\n");
  HASH_ITER(hh, ((JVD*)p)->vml, vm, tmp) {
      off += snprintf(m+off, n-off, "<tr><td>%i</td><td>%i</td><td>%s</td><td>%"PRIlld"</td></tr>\n",
          i++, vm->id, vm->fn?vm->fn:"(null)", (long long)vm->lru);
  }
  off += snprintf(m+off, n-off, "</table>\n");
  pthread_rwlock_unlock(&((JVD*)p)->lock_vml);

  i = 0;
  off += snprintf(m+off, n-off, "<h3>Decoder Objects:</h3>\n");
  off += snprintf(m+off, n-off, "<p>busy: %d%s</p>\n", ((JVD*)p)->busycnt, ((JVD*)p)->purge_in_progress?" (purge queued)":"");
  off += snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off += snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Flags</th><th>Filename</th>"/* "<th>Decoder</th>"*/"<th>PixFmt</th><th>Frame#</th><th>LRU</th></tr>\n");
  off += snprintf(m+off, n-off, "\n");
  while (cptr) {
    char *tmp = flags2txt(cptr->flags);
    char *fn = (cptr->flags&VOF_VALID) ? get_fn((JVD*)p, cptr->id) : NULL;
    off += snprintf(m+off, n-off,
        "<tr><td>%i</td><td>%i</td><td>%s</td><td>%s</td>"/*"<td>%s</td>"*/"<td>%s</td><td>%"PRId64"</td><td>%"PRIlld"</td></tr>\n",
        i++, cptr->id, tmp, fn?fn:"-", /* (cptr->decoder?LIBAVCODEC_IDENT:"null"), */ff_fmt_to_text(cptr->fmt), cptr->frame, (long long)cptr->lru);
    free(tmp);
    cptr = cptr->next;
  }
  off += snprintf(m+off, n-off, "</table>\n");
  return(off);
}

// vim:sw=2 sts=2 ts=8 et:
