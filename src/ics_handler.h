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
#include "jv.h"
#include "socket_server.h"

#ifndef ICSVERSION
#define ICSVERSION VERSION
#endif

/**
 * @brief request parameters
 *
 * request arguments as parsed by the ICS protocol handler
 * mix of JVARGS and JVINFO-request parameters
 *
 * TODO: clean up: either use UNION or reference above structs
 */
typedef struct {
  char *file_name;
  char *save_as;
  jv_framenumber frame;
  int decode_fmt;
  int render_fmt;
  int out_width;
  int out_height;
  int str_audio;
  int str_container;
  int str_vcodec;
  int str_acodec;
  jv_framenumber str_duration;
} ics_request_args;

void ics_http_handler(
		CONN *c,
		char *host, char *protocol,
		char *path, char *method_str,
		char *query, char *cookie
		);
