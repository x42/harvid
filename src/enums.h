/*
   Copyright (C) 2008 Robin Gareus <robin@gareus.org>

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
#ifndef _harvid_enums_H
#define _harvid_enums_H

/* ics_request_args->render_fmt */
enum {
  /* image output format */
	FMT_RAW=0, FMT_JPG, FMT_PNG, FMT_PPM,
  /* info output format */
  OUT_HTML, OUT_JSON, OUT_PLAIN, OUT_CSV
};

/* http index option(s) */
enum {OPT_FLAT=1};

/* cfg_adminmask - binary flags */
enum {ADM_FLUSHCACHE=1, ADM_PURGECACHE=2, ADM_SHUTDOWN=4};

enum {USR_INDEX=1, USR_FLATINDEX=2, USR_KEEPRAW=4, USR_WEBSEEK=8};

#endif
