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

#include "jv.h"
#include "xjv-cache.h"
#include "xjv-ffmpeg.h"

#include <time.h>
#include <assert.h>
#include <pthread.h>

typedef struct videocacheline {
  struct videocacheline *next; // TODO: if cache grows large we may want to use tree-structure or dbl.linked lists.
  int64_t frame;
  time_t lru;
//int usecnt; // TODO: count how many times it has been served
  int id;
  int flags;
  int w;
  int h;
  uint8_t *b; //* data buffer pointer
//void *d; //* custom data buffer struct - render_rmt - non-packed YUV
} videocacheline;

#define SPP (4) // todo: allow for multi-plane data -> custom data-copy func. or sws_copy

static videocacheline *newcl(videocacheline *cache) {
  videocacheline *cl = calloc(1,sizeof(videocacheline));
  //append new line to cache
  videocacheline *cptr = cache;
  while (cptr && cptr->next) cptr=cptr->next;
  if (cptr) cptr->next=cl;
  return(cl);
}

#define CLF_USED 1 //< currently in use (lock)
//#define CLF_NEW 2 //< decoded but but not yet used
//#define CLF_ERR 4 //< error
#define CLF_ALLOC 8 //< ->b is a pointer to some allocated mem. (free)
#define CLF_VALID 16 //< cacheline is valid
//#define CLF_READ 32 //< this cache has been hit at least once after decoding.

/* free and clear a cacheline
 * fails if the cache-line is flagged /CLF_USED/
 * */
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
  // TODO optimize; alloc in ulocked-cache.
  if (i< cfg_cachesize)
    return(newcl(cache));

  cptr=over; // replace LRU
  if (cptr && !(cptr->flags&CLF_USED)) {
    //printf("LRU %d - %lu (frame:%i)\n",i,cptr->lru,cptr->frame);
    // if same w,h - we can keep ALLOC ->  call realloccl_buf() after this fn.
    if (1) {
      cptr->lru=0; cptr->frame=0;
      cptr->flags&=(CLF_ALLOC);
    } else
      assert(! freecl(cptr) );

    return (cptr);
  }

  // all cache lines are in USE/locked - fix your application ;)

  assert(0); // out of cache lines!
  return (NULL);
}

// TODO :: allow to search frame-range (first|exact) - allow smaller/larger than filter
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

/* clear cache information,
 if f==1 force also to flush data (even if it's in USE)
 if f==0 the cache is flushed but data objects are only freed next
 time a cacheline is needed
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

// virtual get_decoder(int id) {;}

typedef struct {
  //JVOBJECT *vsource; -- TODO decoder-object fn pointers.
  //void *in_data; // old decoder plugin API - eg in_data->render()

  int cfg_cachesize;
  videocacheline *vcache;
//int render_fmt;
  pthread_mutex_t lock; // TODO - lock to modify vcache itself
} xjcd;


void xjv_initialize_cache (xjcd *cc) {
  assert(!cc->vcache);
  cc->vcache = newcl(NULL);
//cc->render_fmt = 0;  // TODO set cache's RENDER_FMT.
  pthread_mutex_init(&cc->lock, NULL);
}

void xjv_flush_cache (xjcd *cc) {
  pthread_mutex_lock(&cc->lock);
  clearcache(cc->vcache,1);
  pthread_mutex_unlock(&cc->lock);
}

void xjv_clear_cache (void *p) {
  xjcd *cc = (xjcd*) p;
  pthread_mutex_lock(&cc->lock);
  clearcache(cc->vcache,0);
  pthread_mutex_unlock(&cc->lock);
}

videocacheline *xjv_readcl(xjcd *cc, unsigned long frame, int w, int h, int id) {
  videocacheline *rv = testclwh(cc->vcache, frame, w, h, id, 0);
  if (rv) {
    //fprintf(stderr,"CACHE: using cache!\n");
    rv->lru=time(NULL);
  }

  // if w,h are the same.. -XXX testclwh already checks this.. or not?
  if (rv && w==rv->w && h==rv->h) return(rv);

  // too bad, now we need to allocate a cacheline and then decode the video..
  //printf("CACHE: decoding.. %i\n",frame);
  pthread_mutex_lock(&cc->lock);
  rv=getcl(cc->vcache, cc->cfg_cachesize);
  rv->flags|=CLF_USED;
  pthread_mutex_unlock(&cc->lock);
  realloccl_buf(rv,w,h,SPP); // FIXME ; call ff_get_buffersize()  - use jv_get_info(); jv_decode(..) wrapper API

  // Fill cacheline with data - decode video
  #if 0 // old API - direct decoder interaction
  void *vd = get_decoder(id);
  ff_set_bufferptr(vd, rv->b);
  ff_render(vd, frame, NULL, w, h, 0, w, w);
  #else // new API - TODO THINK: share decoders among sessions?
  int uuid = jv_get_decoder(NULL, -1, id);
  if (uuid < 1) { printf("WARNING: cache thinks it got a bad decoder UUID.\n"); }
  if (jv_decode(NULL, uuid, frame, rv->b, w, h)) {
    rv->flags&=~CLF_VALID;
    rv->flags&=~CLF_USED;
    return (rv); // XXX
  }
  jv_release_decoder(NULL, uuid);
  #endif

  rv->id=id;
  rv->frame=frame;
  // rv->w=w; rv->h=h; - done during realloc_buf
  rv->lru=time(NULL);
  rv->flags|=CLF_VALID;
  rv->flags&=~CLF_USED;
  return(rv);
}


#if 0
//TODO - update to new renderer - get/set buffer & sws_copy
void xjv_render(void *cc, unsigned long frame, uint8_t* buf, int w, int h, int xoff, int xs, int ys) {
  // get data
  videocacheline *cl = xjv_readcl(cc, frame, xs ,h, 0);

  // copy the cache-line to requested buffer
  int x,y,i;
  int xalign =0;
  int yalign =0;
  //copy/crop  cl->b into buf
  for (x=0; ((x+xoff) < cl->w ) && ((x+xalign) < w); x++) {
    for (y=0; y< cl->h && (y+yalign) < h; y++) {
      int d= SPP* ((x+xalign)+ys*(y+yalign));
      int s= SPP* (xoff+x+cl->w*y);
      for (i=0;i<SPP;i++) buf[d+i] = cl->b[s+i];
    }
  }
  // TODO: mark cache as UNUSED ?!
}
#endif

// ---------------------------------------------------------------------------

void cache_create(void **p) {
  (*((xjcd**)p)) = (xjcd*) calloc(1,sizeof(xjcd));
  //(*((xjcd**)p))->in_plugin = (JVPLUGIN*) in_plugin;
  //(*((xjcd**)p))->in_data = in_data;

  (*((xjcd**)p))->cfg_cachesize=48;
  xjv_initialize_cache((*((xjcd**)p)));
}

void cache_resize(void **p, int size) {
  if (size < (*((xjcd**)p))->cfg_cachesize)
    xjv_flush_cache((*((xjcd**)p)));
  if (size>0)
    (*((xjcd**)p))->cfg_cachesize=size;
}

void cache_destroy(void **p) {
  xjcd *cc = *(xjcd**) p;
  xjv_flush_cache(cc);
  pthread_mutex_destroy(&cc->lock);
  free(cc->vcache);
  //(*((xjcd**)p))->in_plugin->destroy((*((xjcd**)p))->in_data);
  free(cc);
  *p=NULL;
}

// TODO - optionally /lock/ cache-line (returned pointer) prevent it being replaced!
uint8_t *cache_get_buffer(void *p, int id, unsigned long frame, int w, int h) {
  videocacheline *cl = xjv_readcl((xjcd*)p, frame, w ,h, id);
  assert(cl);
  // XXX check cl->flags == CLF_VALID|CLF_ALLOC
  return cl->b; // TODO mark as RETRIEVED, update LRU, ..?!
}

int cache_invalidate_buffer(void *p, int id, unsigned long frame, int w, int h) {
  videocacheline *rv = testclwh(((xjcd*)p)->vcache, frame, w, h, id, 0);
  if (rv) {
    freecl(rv);
    return 0;
  }
  return -1;
}

char *flags2txt(int f) {
  char *rv = (char*) calloc(1,sizeof(char));
  size_t off =0;
#if 0
  rv = (char*) realloc(rv, (off+6) * sizeof(char));
  off+=sprintf(rv+off, "%x: ",f);
#endif
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

size_t formatcacheinfo(void *p, char *m, size_t n) {
  size_t off =0;
  videocacheline *cptr = ((xjcd*)p)->vcache;
  off+=snprintf(m+off, n-off, "<h3>Cache Info:</h3>\n");
  off+=snprintf(m+off, n-off, "<p>Size: max. %i entries</p>\n", ((xjcd*)p)->cfg_cachesize);
  off+=snprintf(m+off, n-off, "<table style=\"text-align:center\">\n");
  off+=snprintf(m+off, n-off, "<tr><th>#</th><th>file-id</th><th>Flags</th><th>W</th><th>H</th><th>Frame#</th><th>LRU</th><th>buffer</th></tr>\n");
  int i=0;
  while (cptr) {
    char *tmp = flags2txt(cptr->flags);
    off+=snprintf(m+off, n-off,
        "<tr><td>%d</td><td>%d</td><td>%s</td><td>%d</td><td>%d</td><td>%"PRId64"</td><td>%lld</td><td>%s</td></tr>\n",
	i, cptr->id, tmp, cptr->w, cptr->h, cptr->frame, (long long) cptr->lru,(cptr->b?"alloc":"null"));
    if (tmp) free(tmp);
    i++;
    cptr=cptr->next;
  }
  off+=snprintf(m+off, n-off, "</table>\n");
  return(off);
}

void dumpcacheinfo(void *p) {
  videocacheline *cptr = ((xjcd*)p)->vcache;
  int i=0;
  printf("cache info dump:\n");
  while (cptr) {
    printf("%d,%d,%d,%d,%d,%"PRId64",%lld:%s\n",
	i, cptr->id, cptr->flags, cptr->w, cptr->h, cptr->frame, (long long) cptr->lru, (cptr->b?"allocated":"null"));
    i++;
    cptr=cptr->next;
  }
  printf("------------\n");
}

#if 0
void cache_invalidate(void *p) {
  xjv_clear_cache((*((xjcd**)p)));
}

void cache_flush(void *p) {
  xjv_flush_cache((*((xjcd**)p)));
}
#endif

/* old  paas-thru API

int cache_open(void *p, JVARGS *a) {
  xjcd *cc = (xjcd*) p;
  return (cc->in_plugin->open_movie(cc->in_data, a));
}

int cache_close(void *p) {
  xjcd *cc = (xjcd*) p;
  return (cc->in_plugin->close_movie(cc->in_data));
}

void cache_render(void *p, unsigned long frame, uint8_t* buf, int w, int h, int xoff, int xw, int ys) {
  xjcd *cc = (xjcd*) p;
  cc->in_plugin->render(cc->in_data, frame, buf, w, h, xoff, xw, ys);
}

void cache_get_framerate(void *p, FrameRate *fr){
  xjcd *cc = (xjcd*) p;
  cc->in_plugin->get_framerate(cc->in_data, fr);
}

void cache_get_info(void *p, JVINFO *i){
  xjcd *cc = (xjcd*) p;
  cc->in_plugin->get_info(cc->in_data, i);
}

void cache_init(void) {
  ;
}

void cache_cleanup(void) {
  ;
}
*/

#if 0 // dlopen - plugin interface

static JVPLUGIN cache_UI = {NULL,
  &cache_init, &cache_cleanup,
  &cache_create, &cache_destroy,
  &cache_open, &cache_close,
  &xjv_render,
  &cache_get_info,
  &cache_get_framerate,
  "Image Cache Plugin","0.1"
};

JVPLUGIN *get_plugin_info(void) {
  printf("CACHE: initializing.\n");
  return &cache_UI;
}

#endif


/* vi:set ts=8 sts=2 sw=2: */
