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
#ifndef _XJCD_H
#define _XJCD_H

#include <stdlib.h>
#include <stdint.h>

void *get_decoder(int id); // client needs to provide this function

void cache_create(void **p);
void cache_destroy(void **p);
void cache_resize(void **p, int size);
uint8_t *cache_get_buffer(void *p, int id, unsigned long frame, int w, int h);
void dumpcacheinfo(void *p); // dump debug info to stdout
size_t formatcacheinfo(void *p, char *m, size_t n); // write HTML to m - max length n

void xjv_clear_cache (void *p);
/*
void cache_render(void *p, unsigned long frame,
    uint8_t* buf, int w, int h, int xoff, int xw, int ys);

// TODO cache invalidate. lock/unlock

*/
#endif
