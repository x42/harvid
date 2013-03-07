/*
   This file is part of harvid

   Copyright (C) 2013 Robin Gareus <robin@gareus.org>

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

#include "daemon_log.h"
#include "image_cache.h"

#include <time.h>
#include <assert.h>
#include <pthread.h>

//#define HASH_EMIT_KEYS 3
#define HASH_FUNCTION HASH_SFH
#include "uthash.h"

typedef struct imagecacheline {
  int id;         // file ID from VidMap
  short w;
  short h;
  int fmt;        // image format
  int64_t frame;
  int flags;
  int refcnt;     // CLF_INUSE reference count
  time_t lru;     // east recently used time
  //int hitcount  //  -- unused; least-frequently used idea
  uint8_t *b;     //< data buffer pointer
  UT_hash_handle hh;
} imagecacheline;


void icache_create(void **p) {
	;
}

void icache_destroy(void **p) {
	;
}

void icache_resize(void **p, int size) {
	;
}

void icache_clear (void *p, int id) {
	;
}

uint8_t *icache_get_buffer(void *p, unsigned short id, int64_t frame, int fmt, short w, short h, size_t *size, void **cptr) {

	/* not found in cache */
	if (size) *size = 0;
	if (cptr) *cptr = NULL;
	return NULL;
}

int icache_add_buffer(void *p, unsigned short id, int64_t frame, int fmt, short w, short h, uint8_t *buf, size_t size) {
	return -1; // failed to add
}

void icache_release_buffer(void *p, void *cptr) {
  if (!cptr) return;
}

void icache_info_html(void *p, char **m, size_t *o, size_t *s, int tbl) {
	;
}

