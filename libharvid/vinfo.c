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

#include <string.h>
#include "vinfo.h"

void jvi_init (VInfo *ji) {
  memset (ji, 0, sizeof(VInfo));
  memset(&ji->framerate, 0, sizeof(TimecodeRate));
  ji->framerate.num = 25;
  ji->framerate.den = 1;
  ji->out_width = ji->out_height = -1;
  ji->file_frame_offset = 0.0;
}

void jvi_free (VInfo *i) {
  ;
}

// vim:sw=2 sts=2 ts=8 et:
