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
#ifndef _IMAGE_CACHE_H
#define _IMAGE_CACHE_H

#include <stdlib.h>
#include <stdint.h>

void icache_create(void **p);
void icache_destroy(void **p);
void icache_resize(void *p, int size);
void icache_clear (void *p);

uint8_t *icache_get_buffer(void *p, unsigned short id, int64_t frame, int fmt, int fmt_opt, short w, short h, size_t *size, void **cptr);
int icache_add_buffer(void *p, unsigned short id, int64_t frame, int fmt, int fmt_opt, short w, short h, uint8_t *buf, size_t size);
void icache_release_buffer(void *p, void *cptr);

void icache_info_html(void *p, char **m, size_t *o, size_t *s, int tbl);

#endif
