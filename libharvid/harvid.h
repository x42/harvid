/**
   @file harvid.h
   @brief libharvid -- video decoder/frame-cache

   This file is part of harvid

   @author Robin Gareus <robin@gareus.org>
   @copyright

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
#ifndef _harvid_H
#define _harvid_H

#ifdef __cplusplus
extern "C" {
#endif

/* pixel-format definitions */
#include <libavutil/avutil.h>

/* libharvid public API */
#include "decoder_ctrl.h"
#include "frame_cache.h"
#include "image_cache.h"

/* public ffdecoder.h API */
void ff_initialize (void);
void ff_cleanup (void);
int  picture_bytesize(int render_fmt, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
