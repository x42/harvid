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


/// Video Object Flags
#define VOF_USED 1 //< decoder is currently in use - decoder locked
#define VOF_OPEN 2 //< decoder is idle - decoder is a valid pointer for the file/ID
#define VOF_VALID 4 //<  ID and filename are valid (ID may be in use by cache)
#define VOF_PENDING 8 //< decoder is just opening a file (my_open_movie)
#define VOF_INFO 16 //< decoder is currently in use for info (size/fps) lookup only

typedef struct JVOBJECT {
  struct JVOBJECT *next;
  void *decoder;
  int id;  // this ID is linked to the filename
  time_t lru;
  int64_t frame; // last decoded frame
  pthread_mutex_t lock; // lock to modify flags;
  int flags;
  int infolock_refcnt;
} JVOBJECT;

typedef struct VidMap {
  struct VidMap * next;
  int id;
  char *fn;
  time_t lru;
} VidMap;

typedef struct JVD {
  JVOBJECT *jvo; // list of all decoder objects
  VidMap *vml;   // filename <> id map
  int monotonic;
  int max_objects;
  int cache_size;
  int busycnt;
  int purge_in_progress;
  pthread_mutex_t lock_jvo; // lock to modify (append to) jvo list
  pthread_mutex_t lock_vml; // lock to modify (append to) vid list (inc monotonic)
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

static inline int my_open_movie(void **vd, char *fn) {
  int render_fmt;
  if (!fn) {
    dlog(DLOG_ERR, "DCTL: trying to open file w/o filename.\n");
    return -1;
  }
  ff_create(vd);
/* samples per pixel  -- TODO use render_fmt */
#ifdef VIBER
  #warning Hardcoded YUV420P / YV12
  render_fmt = PIX_FMT_YUV420P;
#elif defined ICSD_RGB24
  #warning Hardcoded 24 bit RGB
  render_fmt = PIX_FMT_RGB24;
#else // mytest
  #warning Hardcoded 32 bit RGBA
  render_fmt = PIX_FMT_RGB32;
#endif

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
  JVOBJECT *n = calloc(1, sizeof(JVOBJECT));
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
static JVOBJECT *testjvd(JVOBJECT *jvo, int id, int64_t frame) {
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
        do {
          pthread_mutex_unlock(&cptr->lock);
          mymsleep(5);
          pthread_mutex_lock(&cptr->lock);
        } while (cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO));
      } else {
        /* we really should not do this */
        dlog(DLOG_ERR, "DCTL: request to free an active decoder.\n");
        cptr->flags &= ~(VOF_USED|VOF_PENDING|VOF_INFO);
        cptr->infolock_refcnt = 0; // XXX will trigger assert() on dctrl_release_infolock()
      }
    }

    if (cptr->flags&VOF_OPEN) {
      my_destroy(&cptr->decoder);
      cptr->decoder=NULL;
      cptr->flags &= ~VOF_OPEN;
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
  int i = 0;
  JVOBJECT *dec_closed = NULL;
  JVOBJECT *dec_open = NULL;
  time_t lru = time(NULL) + 1;
  JVOBJECT *cptr = jvd->jvo;
  clearjvo(jvd, 1, -1, 600, &jvd->lock_jvo); // garbage collect
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
    i++;
  }

  if (dec_closed) {
    cptr = dec_closed;
  } else if (dec_open) {
    cptr = dec_open;
  }

  if (cptr && !pthread_mutex_trylock(&cptr->lock)) {
      if (!(cptr->flags&(VOF_USED|VOF_PENDING|VOF_INFO))) {

        if (cptr->flags&(VOF_OPEN)) {
          my_destroy(&cptr->decoder); // close it.
          dec_open->decoder = NULL; // not really need..
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

  if (i < jvd->max_objects)
    return(newjvo(jvd->jvo, &jvd->lock_jvo));
  return (NULL);
}

///////////////////////////////////////////////////////////////////////////////
// Video decoder management
//

static VidMap *newvid(VidMap *vml, int max_size) {
  VidMap *vptr = vml, *vlru = NULL, *vlast = vml;
  int i = 0;
  time_t lru = time(NULL) + 1;
  while (vptr) {
    if (vptr->lru == 0) return vptr;
    if (vptr->lru < lru) {
      lru = vptr->lru;
      vlru = vptr;
    }
    vlast = vptr;
    vptr = vptr->next;
    i++;
  }
  if (i < max_size) {
    VidMap *n = calloc(1, sizeof(VidMap));
    if (vlast) vlast->next = n;
    return(n);
  } else {
    vlru->lru = 0;
    vlru->id = 0;
    free(vlru->fn);
    vlru->fn=NULL;
    return vlru;
  }
}

static void clearvid(VidMap *vml, pthread_mutex_t *vmllock) {
  VidMap *vptr = vml;
  pthread_mutex_lock(vmllock);
  while (vptr) {
    VidMap *vnext = vptr->next;
    free(vptr->fn);
    vptr->next=NULL;
    free(vptr);
    vptr = vnext;
  }
  pthread_mutex_unlock(vmllock);
}

static VidMap *searchvidmap(VidMap *vml, int cmpmode, int id, const char *fn) {
  VidMap *vptr;
  for (vptr = vml; vptr ; vptr = vptr->next) {
    if (    ((cmpmode&1) == 1 || vptr->id == id)
        &&  ((cmpmode&2) == 2 || (vptr->fn && !strcmp(vptr->fn, fn)))
        &&  vptr->lru > 0
       )
      return(vptr);
  }
  return(NULL);
}

static int get_id(JVD *jvd, const char *fn) {
  while (1) {
    VidMap *vm = searchvidmap(jvd->vml, 1, 0, fn);
    if (vm) {
      pthread_mutex_lock(&jvd->lock_vml);
      if (vm->lru == 0) {
        pthread_mutex_unlock(&jvd->lock_vml);
        continue;
      }
      vm->lru=time(NULL);
      pthread_mutex_unlock(&jvd->lock_vml);
      return vm->id;
    }

    /* add new entry */
    if (pthread_mutex_trylock(&jvd->lock_vml)) {
      /* check that no other thread got ahead of us */
      vm = searchvidmap(jvd->vml, 1, 0, fn);
      if (vm) {
        pthread_mutex_unlock(&jvd->lock_vml);
        return vm->id;
      }
      vm = newvid(jvd->vml, jvd->cache_size);
      vm->id = jvd->monotonic++;
      vm->fn = strdup(fn);
      vm->lru=time(NULL);
      pthread_mutex_unlock(&jvd->lock_vml);
      return vm->id;
    } else {
      sched_yield();
    }
  }
  return -1; // never reached
}

static char *get_fn(JVD *jvd, int id) {
  VidMap *vm;
  do {
    vm = searchvidmap(jvd->vml, 2, id, NULL);
    if (!vm) return (NULL);
    pthread_mutex_lock(&jvd->lock_vml);
    if (vm->lru == 0) {
      pthread_mutex_unlock(&jvd->lock_vml);
      continue;
    }
    pthread_mutex_unlock(&jvd->lock_vml);
  } while (!vm);
  return vm->fn;
}

static void release_id(JVD *jvd, int id) {
  pthread_mutex_lock(&jvd->lock_vml);
  VidMap *vm = searchvidmap(jvd->vml, 2, id, NULL);
  if (vm) {
    vm->lru=0;
    vm->id = 0;
    free(vm->fn);
    vm->fn=NULL;
  }
  pthread_mutex_unlock(&jvd->lock_vml);
}


static JVOBJECT *new_video_object(JVD *jvd, int id) {
  JVOBJECT *jvo;
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
static void * dctrl_get_decoder(void *p, int id, int64_t frame) {
  JVD *jvd = (JVD*)p;
  BUSYADD(jvd)

tryagain:
  debugmsg(DEBUG_DCTL, "DCTL: get_decoder fileid=%i\n", id);

  JVOBJECT *jvo;
  int timeout = 40; // new_video_object() delays 5ms at a time.
  do {
    jvo = testjvd(jvd->jvo, id, frame);
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
    if (!my_open_movie(&jvo->decoder, get_fn(jvd, jvo->id))) {
      pthread_mutex_lock(&jvo->lock);
      jvo->flags |= VOF_OPEN;
      jvo->flags &= ~VOF_PENDING;
      pthread_mutex_unlock(&jvo->lock);
    } else {
      pthread_mutex_lock(&jvo->lock);
      jvo->flags &= ~VOF_PENDING;
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
  pthread_mutex_init(&jvd->lock_vml, NULL);

  jvd->vml = newvid(NULL, jvd->cache_size);
  jvd->jvo = newjvo(NULL, &jvd->lock_jvo);
}

void dctrl_destroy(void **p) {
  JVD *jvd = (*((JVD**)p));
  clearjvo(jvd, 3, -1, -1, &jvd->lock_jvo);
  clearvid(jvd->vml, &jvd->lock_vml);
  pthread_mutex_destroy(&jvd->lock_busy);
  pthread_mutex_destroy(&jvd->lock_jvo);
  pthread_mutex_destroy(&jvd->lock_vml);
  free(jvd->jvo);
  free(*((JVD**)p));
  *p = NULL;
}

int dctrl_get_id(void *p, const char *fn) {
  JVD *jvd = (JVD*)p;
  return get_id(jvd, fn);
}


int dctrl_decode(void *p, int id, int64_t frame, uint8_t *b, int w, int h) {
  void *dec = dctrl_get_decoder(p, id, frame);
  if (!dec) {
    dlog(DLOG_WARNING, "DCTL: no decoder available.\n");
    return -1;
  }
  int rv = xdctrl_decode(dec, frame, b, w, h);
  dctrl_release_decoder(dec);
  return (rv);
}

int dctrl_get_info(void *p, int id, VInfo *i) {
  JVOBJECT *jvo = (JVOBJECT*) dctrl_get_decoder(p, id, -1);
  if (!jvo) return -1;
  my_get_info(jvo->decoder, i);
  dctrl_release_infolock(jvo);
  return(0);
}

int dctrl_get_info_scale(void *p, int id, VInfo *i, int w, int h) {
  JVOBJECT *jvo = (JVOBJECT*) dctrl_get_decoder(p, id, -1);
  if (!jvo) return -1;
  my_get_info_canonical(jvo->decoder, i, w, h);
  dctrl_release_infolock(jvo);
  return(0);
}

void dctrl_cache_clear(void *p, int f, int id) {
  JVD *jvd = (JVD*)p;
  clearjvo(jvd, f, id, -1, &jvd->lock_jvo);
}

///////////////////////////////////////////////////////////////////////////////
// 2c - video object/decoder stats - public API

static char *flags2txt(int f) {
  char *rv = NULL;
  size_t off = 0;

  if (f==0) {
    rv = (char*) realloc(rv, (off+2) * sizeof(char));
    off+=sprintf(rv+off, "-");
    return rv;
  }
  if (f&VOF_USED) {
    rv = (char*) realloc(rv, (off+6) * sizeof(char));
    off+=sprintf(rv+off, "used ");
  }
  if (f&VOF_OPEN) {
    rv = (char*) realloc(rv, (off+6) * sizeof(char));
    off+=sprintf(rv+off, "open ");
  }
  if (f&VOF_VALID) {
    rv = (char*) realloc(rv, (off+7) * sizeof(char));
    off+=sprintf(rv+off, "hasID ");
  }
  if (f&VOF_PENDING) {
    rv = (char*) realloc(rv, (off+9) * sizeof(char));
    off+=sprintf(rv+off, "pending ");
  }
  if (f&VOF_INFO) {
    rv = (char*) realloc(rv, (off+9) * sizeof(char));
    off+=sprintf(rv+off, "info ");
  }
  return rv;
}

size_t dctrl_info_html (void *p, char *m, size_t n) {
  JVOBJECT *cptr = ((JVD*)p)->jvo;
  VidMap *vptr = ((JVD*)p)->vml;
  int i = 0;
  size_t off = 0;
  off+=snprintf(m+off, n-off, "<h3>Decoder Objects:</h3>\n");
  off+=snprintf(m+off, n-off, "<p>busy: %d%s</p>\n", ((JVD*)p)->busycnt, ((JVD*)p)->purge_in_progress?" (purge queued)":"");
  off+=snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off+=snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Flags</th><th>Filename</th><th>LRU</th><th>decoder</th><th>frame#</th></tr>\n");
  off+=snprintf(m+off, n-off, "\n");
  while (cptr) {
    char *tmp = flags2txt(cptr->flags);
    char *fn = get_fn((JVD*)p, cptr->id);
    off+=snprintf(m+off, n-off,
        "<tr><td>%i</td><td>%i</td><td>%s</td><td>%s</td><td>%"PRIlld"</td><td>%s</td><td>%"PRId64"</td></tr>\n",
        i++, cptr->id, tmp, fn?fn:"-", (long long)cptr->lru, (cptr->decoder?LIBAVCODEC_IDENT:"null"), cptr->frame);
    free(tmp);
    cptr = cptr->next;
  }
  off+=snprintf(m+off, n-off, "</table>\n");

  i=0;
  off+=snprintf(m+off, n-off, "<h3>File Mapping:</h3>\n");
  off+=snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off+=snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Filename</th><th>LRU</th></tr>\n");
  off+=snprintf(m+off, n-off, "\n");
  while (vptr) {
      off+=snprintf(m+off, n-off, "<tr><td>%i</td><td>%i</td><td>%s</td><td>%"PRIlld"</td></tr>\n",
          i++, vptr->id, vptr->fn?vptr->fn:"(null)", (long long)vptr->lru);
    vptr = vptr->next;
  }

  off+=snprintf(m+off, n-off, "</table>\n");

  return(off);
}

void dctrl_info_dump(void *p) {
  JVOBJECT *cptr = ((JVD*)p)->jvo;
  int i = 0;
  printf("decoder info dump:\n");
  while (cptr) {
    char *fn = get_fn((JVD*)p, cptr->id);
    printf("%i,%i,%i,%s,%lu:%s:%"PRId64"\n",
        i, cptr->id, cptr->flags, fn?fn:"-", cptr->lru, (cptr->decoder?"open":"null"), cptr->frame);
    i++;
    cptr = cptr->next;
  }
  printf("------------\n");
}

// vim:sw=2 sts=2 ts=8 et:
