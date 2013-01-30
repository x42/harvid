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
#ifndef _writer_H
#define _writer_H
#include "jv.h"
enum {OUT_FMT_JPEG=1, OUT_FMT_PNG=2, OUT_FMT_PPM=3};

void write_image(JVARGS *ja, JVINFO *ji, uint8_t *buf);
long int format_image(uint8_t **out, JVARGS *ja, JVINFO *ji, uint8_t *buf);

#endif
