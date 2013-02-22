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
#ifndef _image_format_H
#define _image_format_H
#include "vinfo.h"

/** write image to memory-buffer 
 * @param out pointer to memory-area for the formatted image
 * @param ji input data description (width, height, stride,..)
 * @param buf raw image data to format
 */
size_t format_image(uint8_t **out, int render_fmt, int misc_int, VInfo *ji, uint8_t *buf);

/** write image to file
 * @param ji input data description (width, height, stride,..)
 * @param buf raw image data to format
 */
void write_image(char *file_name, int render_fmt, VInfo *ji, uint8_t *buf);


#endif
