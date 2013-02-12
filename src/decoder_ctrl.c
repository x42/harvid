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
#include "decoder_ctrl.h"
#include "ffdecoder.h"
#include "ffcompat.h"
#include <assert.h>

#include "daemon_log.h"

static int my_decode(void *vd, unsigned long frame, uint8_t *b, int w, int h) {
  //printf("JV: decode into buffer.\n");
  ff_resize(vd, w, h, NULL, NULL);
  ff_set_bufferptr(vd, b);
  // TODO: if check rendering failed !!
  ff_render(vd, frame, NULL, w, h, 0, w, w);
  ff_set_bufferptr(vd, NULL);
  return 0;
}

static int my_open_movie(void **vd, char *fn) {
  ff_create(vd);
  int render_fmt;
#ifdef VIBER
  #warning Hardcoded YUV420P / YV12
  render_fmt = PIX_FMT_YUV420P; // TODO allow to set, override
#elif defined ICSD_RGB24
  #warning Hardcoded 24 bit RGB
  render_fmt = PIX_FMT_RGB24; // TODO allow to set, override
#else // mytest
  #warning Hardcoded 32 bit RGBA
  render_fmt = PIX_FMT_RGB32;
#endif

  if (!ff_open_movie (*vd, fn, render_fmt)) {
    dlog(DLOG_DEBUG, "JV : opened video file: '%s'\n", fn);
  } else {
    dlog(DLOG_ERR, "JV : error opening video file.\n");
    ff_destroy(vd);
    return(1); // TODO cleanup - destroy ff ..
  }
  return(0);
}

static int my_destroy(void **vd) {
  ff_destroy(vd);
  return(0);
}

static void my_get_info(void *vd, VInfo *i) {
  ff_get_info(vd, i);
}

static void my_get_info_scale(void *vd, VInfo *i, int w, int h) {
  ff_resize(vd, w, h, NULL, i);
}

///////////////////////////////////////////////////////////
// Video object management

// part 1 - internal list mgmnt.

/// Video Object Flags
#define VOF_USED 1 //< decoder is currently in use - decoder locked
#define VOF_OPEN 2 //< decoder is idle - decoder is a valid pointer for the file/ID
#define VOF_VALID 4 //<  ID and filename are valid (ID may be in use by cache)
//#define VOF_EXCLUSIVE 8 //< TODO - use uuid to address this decoder and not id=filename
//#define VOF_NOLRU 16 //< TODO - don't close this decoder even if it's unused and the cache is full ;(

typedef struct JVOBJECT {
  struct JVOBJECT *next;
  void *decoder;
  char *fn;
  int id;  // this ID is linked to the filename
  int uuid;  // unique ID for this decoder
  time_t lru;
  int64_t frame; // last decoded frame
  pthread_mutex_t lock; // lock to modify flags;
  int flags;
} JVOBJECT;

static JVOBJECT *newjvo (JVOBJECT *jvo) {
// TODO lock parent JVD before appending to list ?!
  JVOBJECT *n = calloc(1, sizeof(JVOBJECT));
  pthread_mutex_init(&n->lock, NULL);
  JVOBJECT *cptr = jvo;
  while (cptr && cptr->next) cptr = cptr->next;
  if (cptr) cptr->next = n;
  return(n);
}

// TODO: allow to find more than one.. by file-id.. flags=NOT_USED, prefer open..
static JVOBJECT *testjvd(JVOBJECT *jvo, int id, int64_t frame) {
  JVOBJECT *cptr = jvo;
  JVOBJECT *dec_closed = NULL;
  JVOBJECT *dec_open = NULL;
  time_t lru_open = time(NULL) + 1;
  time_t lru_closed = time(NULL) + 1;
  int64_t framediff = -1; // TODO prefer decoders w/ frame before but close to frame
  int found = 0, avail = 0; // DEBUG
  for (;cptr; cptr = cptr->next) {
    if (!(cptr->flags&VOF_VALID) || cptr->id != id) {
      continue;
    }

    found++; // DEBUG

    if (cptr->flags&VOF_USED) {
      continue;
    }

    avail++; // DEBUG

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
    } else {
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
  }

  dlog(LOG_INFO, "JV : found %d avail. from %d total decoder(s) for file-id:%d. [%s]\n",
      avail, found, id, dec_open?"open":dec_closed?"closed":"N/A");
  if (dec_open) {
    return(dec_open);
  }
  if (dec_closed) {
    return(dec_closed);
  }
  return(NULL);
}

static JVOBJECT *testjvo(JVOBJECT *jvo, int cmpmode, int id, const char *fn) {
  JVOBJECT *cptr = jvo;
  while (cptr) {
    if (    ((cmpmode&1) == 1 || cptr->id == id)
        &&  ((cmpmode&2) == 2 || ((cptr->flags&VOF_VALID) && cptr->fn && !strcmp(cptr->fn, fn)))
        &&  ((cmpmode&4) == 0 || cptr->flags == 0)
        &&  ((cmpmode&8) == 0 || cptr->uuid == id)
       )
      return(cptr);
    cptr = cptr->next;
  }
  return(NULL);
}

#if 0 // yet unused -- b0rked
// garbage collect decoder objects
static void gc_jvo(JVOBJECT *jvo, int min_age) {
  JVOBJECT *cptr = jvo;
  time_t lru = time(NULL);
  while (cptr) {
    if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID)) == 0) {
      cptr = cptr->next;
      continue;
    }
    if ((cptr->flags&VOF_USED) != 0 || cptr->lru + min_age > lru) {
      cptr = cptr->next;
      continue;
    }
    dlog(DLOG_CRIT, "JV : GARBAGE COLLECTOR\n");
    pthread_mutex_lock(&cptr->lock); // TODO check flags again after taking lock !
    my_destroy(&cptr->decoder); // close it.
    cptr->decoder = NULL; // not really need..
    if (cptr->fn) free(cptr->fn);
    cptr->fn = NULL;
    cptr->flags &= ~(VOF_VALID|VOF_OPEN);
    cptr->lru = 0; cptr->id = cptr->uuid = 0;
    pthread_mutex_unlock(&cptr->lock);

    cptr = cptr->next;
  }
}
#endif

//get some unused allocated jvo or create one.
static JVOBJECT *getjvo(JVOBJECT *jvo, int max_objects) {
  int i = 0;
  JVOBJECT *dec_closed = NULL;
  JVOBJECT *dec_open = NULL;
  time_t lru = time(NULL) + 1;
  JVOBJECT *cptr = jvo;
  // gc_jvo (jvo, 600);  // disabled until fixed
  while (cptr) {
    if ((cptr->flags&(VOF_USED|VOF_OPEN|VOF_VALID)) == 0) return (cptr);

    if (!(cptr->flags&(VOF_USED|VOF_OPEN)) && (cptr->lru < lru)) {
      lru = cptr->lru;
      dec_closed = cptr;
    } else if (!(cptr->flags&(VOF_USED)) && (cptr->lru < lru)) {
      lru = cptr->lru;
      dec_open = cptr;
    }
    cptr = cptr->next;
    i++;
  }
  if (i < max_objects)
    return(newjvo(jvo));

  if (dec_closed)
    cptr = dec_closed; // replace LRU
  else if (dec_open) {
    cptr = dec_open; // replace with LRU decoder that's still open.
    pthread_mutex_lock(&cptr->lock); // TODO check flags again after taking lock !
    my_destroy(&cptr->decoder); // close it.
    dec_open->decoder = NULL; // not really need..
    cptr->flags &= ~VOF_OPEN;
    pthread_mutex_unlock(&cptr->lock);
  }

  // reset LRU or invalidate
  if (cptr && !(cptr->flags&VOF_USED)) {
    //printf("JV - LRU %d - %lu\n", i, cptr->lru);
    pthread_mutex_lock(&cptr->lock); // TODO check flags again after taking lock !
    if (cptr->fn) free(cptr->fn);
    cptr->id = 0;
    cptr->fn = NULL;
    cptr->lru = 0;
    cptr->uuid = 0;
    cptr->flags = 0;
    pthread_mutex_unlock(&cptr->lock);
    return (cptr);
  }

  assert(0); // out of memory..
  return (NULL);
}

/* clear object information,
 if f==3 force also to flush data (even if it's in USE)
 if f==2 all /unused objects/ are invalidated and freed.
 if f==1 all /unused objects/ are invalidated.
 if f==0 all /unused and open objects/ are closed.
 time a cacheline is needed
 @param id ; filter on id ; -1: all in cache.
*/
static int clearjvo(JVOBJECT *jvo, int f, int id) {
  JVOBJECT *cptr = jvo;
  int busy = 0, cleared = 0, freed = 0, count = 0, skipped = 0; // DEBUG
  while (cptr) {
    if (id > 0 && cptr->id != id) {
      cptr = cptr->next;
      skipped++;
      continue;
    }
    JVOBJECT *mem = cptr;
    count++;

    pthread_mutex_lock(&cptr->lock); // TODO check flags again after taking lock !
    if (cptr->flags&VOF_USED) {
      if (f < 3) { // FIXME - set jvo->next pointer
	pthread_mutex_unlock(&cptr->lock);
	busy++;
	cptr = cptr->next;
	continue;
      }
      dlog(DLOG_CRIT, "JV : WARNING - requesting to free a used decoder.\n");
      cptr->flags &= ~VOF_USED;
    }

    if (cptr->flags&VOF_OPEN) {
      my_destroy(&cptr->decoder);
      cptr->flags &= ~VOF_OPEN;
    }

    if (f > 0) {
      if (cptr->fn) free(cptr->fn);
      cptr->fn = NULL;
      cptr->id = 0;
      cptr->lru = 0;
      cptr->uuid = 0;
      cptr->flags &= ~VOF_VALID;
      cleared++;
    }

    pthread_mutex_unlock(&cptr->lock);

    cptr = cptr->next;
    mem->next = NULL;
    if (f > 1 && mem != jvo) {free(mem); freed++;}
  }
  if (f > 2) jvo->next = NULL;

  dlog(LOG_INFO, "JV : GC processed %d VObj, skipped: %d, freed: %d, cleared: %d - busy: %d\n", count, skipped, freed, cleared, busy);
  return (cleared);
}

// --- API

typedef struct JVD {
  JVOBJECT *jvo; // list of all decoder objects
  int max_objects;
  int max_open_files;
  int max_concurrent;
  pthread_mutex_t lock; // lock to read/increment monotonic;
  int monotonic;
} JVD;

//NOTE: id and filename are equivalent (!)
// a numeric id is just more handy to compare/pass around.
static JVOBJECT *get_obj(JVD *jvd, int uuid) {
  return(testjvo(jvd->jvo, 11, uuid, NULL));
}

static int get_id(JVD *jvd, const char *fn) {
  JVOBJECT *jvo = testjvo(jvd->jvo, 1, 0, fn);
  if (jvo) return (jvo->id);
  return (-1);
}

static char *get_fn(JVD *jvd, int id) {
  JVOBJECT *jvo = testjvo(jvd->jvo, 2, id, NULL);
  if (jvo) return (jvo->fn); // strdup ?!
  return (NULL);
}

static int new_video_object_fn(JVD *jvd, const char *fn) {
  JVOBJECT *jvo = getjvo(jvd->jvo, jvd->max_objects);
  if (!jvo) return (-1); // LATER - for now getjvo will assert(0)
  pthread_mutex_lock(&jvo->lock); // TODO trylock - and check flags again
  jvo->id = get_id(jvd, fn);  // zero is allowed here
  pthread_mutex_lock(&jvd->lock);  // lock monotonic
  if (jvo->id < 0) jvo->id = jvd->monotonic;
  jvo->uuid = jvd->monotonic++;
  pthread_mutex_unlock(&jvd->lock);  // unlock monotonic
  jvo->flags |= VOF_VALID;
  jvo->fn = strdup(fn);
  jvo->frame = -1;
  pthread_mutex_unlock(&jvo->lock);
  return(jvo->id); // ID
}

#if 0 // unused
/**
 * use-case: re-open a file. - flush decoders (and indirectly cache -> new ID)
 */
static int release_video_object(JVD *jvd, char *fn) {
  int id;
  if ((id = get_id(jvd, fn)) > 0)
    return (clearjvo(jvd->jvo, 1, id)); // '0': would close only close and re-use the same ID (cache)
  return(-1);
}
#endif

//////////////////////////////////////////////////
// part 2 - video object API - top part

JVD *g_decoder; // global decoder pointer - TODO: get from session or daemon-instance.

void dctrl_create(void **p) {
  (*((JVD**)p)) = (JVD*) calloc(1, sizeof(JVD));
  (*((JVD**)p))->monotonic = 1;
  pthread_mutex_init(&((*((JVD**)p))->lock), NULL);

  (*((JVD**)p))->jvo = newjvo(NULL);
  (*((JVD**)p))->max_objects = 1024;
  (*((JVD**)p))->max_open_files = 64;// unused
  (*((JVD**)p))->max_concurrent = 2; // unused

  g_decoder = (*((JVD**)p)); // temp hack for testing.
}

void dctrl_destroy(void **p) {
  clearjvo((*((JVD**)p))->jvo, 4, -1);
  free((*((JVD**)p))->jvo);
  free(*((JVD**)p));
  *p = NULL;
}


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
    off+=sprintf(rv+off, "valid ");
  }
  return rv;
}

size_t dctrl_info_html (void *p, char *m, size_t n) {
  JVOBJECT *cptr = ((JVD*)p)->jvo;
  int i = 0;
  size_t off = 0;
  off+=snprintf(m+off, n-off, "<h3>Decoder Objects:</h3>\n");
  off+=snprintf(m+off, n-off, "<table style=\"text-align:center;width:100%%\">\n");
  off+=snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Flags</th><th>Filename</th><th>decoder-id</th><th>LRU</th><th>decoder</th><th>frame#</th></tr>\n");
  off+=snprintf(m+off, n-off, "\n");
  while (cptr) {
    char *tmp = flags2txt(cptr->flags);
    off+=snprintf(m+off, n-off,
        "<tr><td>%i</td><td>%i</td><td>%s</td><td>%s</td><td>%i</td><td>%"PRIlld"</td><td>%s</td><td>%"PRId64"</td></tr>\n",
        i, cptr->id, tmp, (cptr->fn?cptr->fn:"-"), cptr->uuid, (long long)cptr->lru, (cptr->decoder?"open":"null"), cptr->frame);
    free(tmp);
    i++;
    cptr = cptr->next;
  }
  off+=snprintf(m+off, n-off, "</table>\n");
  return(off);
}

void dctrl_info_dump(void *p) {
  JVOBJECT *cptr = ((JVD*)p)->jvo;
  int i = 0;
  printf("decoder info dump:\n");
  while (cptr) {
    printf("%i,%i,%i,%s,%i,%lu:%s:%"PRId64"\n",
        i, cptr->id, cptr->flags, cptr->fn, cptr->uuid, cptr->lru, (cptr->decoder?"open":"null"), cptr->frame);
    i++;
    cptr = cptr->next;
  }
  printf("------------\n");
}


int dctrl_get_id(void *p, const char *fn) {
  JVD *jvd = p ? (JVD*)p : g_decoder;
  int id;
  // TODO ; prepare new decoder objects on the fly already here..
  if ((id = get_id(jvd, fn)) >= 0) return id;
  return new_video_object_fn(jvd, fn);
}

// part 2b - video object API - decoder interaction

int dctrl_get_info(void *p, int id, VInfo *i) {
  JVD *jvd = p ? (JVD*)p : g_decoder;
  JVOBJECT *jvo = NULL;
#if 0 // FIXME
  // TODO: test for open decoder for this file-ID
  jvo = testjvd(jvd->jvo, id); // TODO open if not opened
  // TODO; allow uuid exclusive lookups
#else
  int uuid = dctrl_get_decoder(p, id, -1);
  jvo = get_obj(jvd, uuid);
#endif
  if (jvo) {
    my_get_info(jvo->decoder, i);
    dctrl_release_decoder(p, uuid);
    return(0);
  }
  return(-1);
}

int dctrl_get_info_scale(void *p, int id, VInfo *i, int w, int h) {
  JVD *jvd = p ? (JVD*)p : g_decoder;
  JVOBJECT *jvo  = NULL;
#if 0 // FIXME
  // TODO: test for open decoder for this file-ID
  jvo = testjvd(jvd->jvo, id); // TODO open if not opened
  // TODO; allow uuid exclusive lookups
#else
  int uuid = dctrl_get_decoder(p, id, -1);
  jvo = get_obj(jvd, uuid);
#endif
  if (jvo) {
    my_get_info_scale(jvo->decoder, i, w, h);
    dctrl_release_decoder(p, uuid);
    return(0);
  }
  return(-1);
}

// lookup or create new decoder for file ID
int dctrl_get_decoder(void *p, int id, int64_t frame) {
  JVD *jvd = p ? (JVD*)p : g_decoder;

tryagain:
  dlog(DLOG_DEBUG, "JV : get_decoder fileid=%i\n", id);

  JVOBJECT *jvo;
  int timeout = 10;
  while (--timeout > 0 && !(jvo = testjvd(jvd->jvo, id, frame))) {
    char *fn;
    if ((fn = get_fn(jvd, id))) {
      // TODO - check number of active decoders, .. sleep .. ? return (-1);
      new_video_object_fn(jvd, fn);
      // if ((jvo = testjvd(jvd->jvo, id)) break;  // better than ++timeout;
      continue;
    } else {
      return(-1); // we're screwed without a filename
    }
    //.. wait until a decoder becomes available..
#ifdef HAVE_WINDOWS
    Sleep(1);
#else
    usleep(1);
#endif
    if (timeout == 8)
      dlog(DLOG_DEBUG, "JV : waiting for decoder...\n");
  }

  if (!jvo) {
    dlog(DLOG_ERR, "JV : ERROR: no decoder object available.\n");
    return(-1);
  }

  pthread_mutex_lock(&jvo->lock);

  if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID)) == (VOF_VALID)) {
    if (!my_open_movie(&jvo->decoder, jvo->fn))
      jvo->flags |= VOF_OPEN;
    else {
      pthread_mutex_unlock(&jvo->lock);
      dlog(DLOG_ERR, "JV : ERROR: opening of movie file failed.\n");
      return(-1);
    }
  }

  if ((jvo->flags&(VOF_USED|VOF_OPEN|VOF_VALID)) == (VOF_VALID|VOF_OPEN)) {
    jvo->flags |= VOF_USED;
    pthread_mutex_unlock(&jvo->lock);
    return(jvo->uuid);
  }

  pthread_mutex_unlock(&jvo->lock);

  dlog(DLOG_DEBUG, "JV : WARNING: decoder object was busy.\n");
  goto tryagain;

  return(-1);
}

//called by fc_readcl()
int dctrl_decode(void *p, int uuid, unsigned long frame, uint8_t *b, int w, int h) {
  JVD *jvd = p ? (JVD*)p : g_decoder;
  JVOBJECT *jvo;
  //printf("dctrl_decode: decoder: %i frame:%lu - %x \n", uuid, frame, b);
  if (!(jvo = get_obj(jvd, uuid))) return -1;
  jvo->lru = time(NULL);
  int rv= my_decode(jvo->decoder, frame, b, w, h);
  if (!rv) {
    jvo->frame = frame;
  }
  //printf("dctrl_finish: decoder: %i frame:%lu\n", uuid, frame);
  return rv;
}

void dctrl_release_decoder(void *p, int uuid) {
  JVD *jvd = p ? (JVD*)p : g_decoder;
  JVOBJECT *jvo;
  if (!(jvo = get_obj(jvd, uuid))) return;
  pthread_mutex_lock(&jvo->lock);
  jvo->flags &= ~VOF_USED;
  pthread_mutex_unlock(&jvo->lock);
  //printf("released_decoder: %d\n", jvo->uuid);
}

// vim:sw=2 sts=2 ts=8 et:
