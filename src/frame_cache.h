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
#ifndef _FRAME_CACHE_H
#define _FRAME_CACHE_H

#include <stdlib.h>
#include <stdint.h>

void vcache_create(void **p);
void vcache_destroy(void **p);
void vcache_resize(void **p, int size);
void vcache_clear (void *p);

uint8_t *vcache_get_buffer(void *p, void *dc, int id, int64_t frame, int w, int h, int fmt, void **cptr);
void vcache_release_buffer(void *p, void *cptr);

void vcache_info_dump(void *p); // dump debug info to stdout
size_t vcache_info_html(void *p, char *m, size_t n); // write HTML to m - max length n

#endif
