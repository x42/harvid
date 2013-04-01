/*
   This file is part of harvid

   Copyright (C) 2007-2013 Robin Gareus <robin@gareus.org>

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
#ifndef _FFDECODER_H
#define _FFDECODER_H

#include <stdint.h>

void ff_create(void **ff);
void ff_destroy(void **ff);
void ff_get_info(void *ptr, VInfo *i);
void ff_get_info_canonical(void *ptr, VInfo *i, int w, int h);

int ff_render(void *ptr, unsigned long frame,
    uint8_t* buf, int w, int h, int xoff, int xw, int ys);

int ff_open_movie(void *ptr, char *file_name, int render_fmt);
int ff_close_movie(void *ptr);

void ff_initialize (void);
void ff_cleanup (void);

uint8_t *ff_get_bufferptr(void *ptr);
uint8_t *ff_set_bufferptr(void *ptr, uint8_t *buf);
void ff_resize(void *ptr, int w, int h, uint8_t *buf, VInfo *i);

int ff_picture_bytesize(int render_fmt, int w, int h);
const char * ff_fmt_to_text(int fmt);
#endif
