/**
   @file vinfo.h
   @brief video information

   This file is part of harvid

   @author Robin Gareus <robin@gareus.org>
   @copyright

   Copyright (C) 2002,2003,2008-2013 Robin Gareus <robin@gareus.org>

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
#ifndef _VINFO_H
#define _VINFO_H

#include <stdlib.h>
#include <stdint.h> /* uint8_t */
#include "timecode.h"

/** video-file information */
typedef struct {
  int movie_width;        ///< read-only image-size
  int movie_height;       ///< read-only image-size
  double movie_aspect;    ///< read-only original aspect-ratio
  int out_width;          ///< actual output image geometry
  int out_height;         ///< may be overwritten
  TimecodeRate framerate; ///< framerate num/den&flags
  int64_t frames;  ///< duration of file in frames
  size_t buffersize;      ///< size in bytes used for an image of out_width x out_height at render_rmt (VInfo)
  double file_frame_offset;
} VInfo;

/** initialise a VInfo struct
 * @param i VInfo struct to initialize
 */
void jvi_init (VInfo*i);
/** clear and free VInfo struct
 * @param i VInfo struct to free
 */
void jvi_free (VInfo*i);

#endif
