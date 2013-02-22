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
#ifndef _ics_handler_H
#define _ics_handler_H

#include "socket_server.h"

/**
 * @brief request parameters
 *
 * request arguments as parsed by the ICS protocol handler
 * mix of JVARGS and VInfo-request parameters
 */
typedef struct {
  char *file_name;
  char *file_qurl;
  int64_t frame;
  int decode_fmt;
  int render_fmt;
  int out_width;
  int out_height;
  int idx_option;
  int misc_int; // currently used for jpeg quality only
} ics_request_args;

void ics_http_handler(
  CONN *c,
  char *host, char *protocol,
  char *path, char *method_str,
  char *query, char *cookie
  );
#endif
