/**
   @file decoder_ctrl.h
   @brief video object abstraction

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
#ifndef _DECODER_CTRL_H
#define _DECODER_CTRL_H

#include "vinfo.h"

/** create and allocate a decoder control object
 * @param p pointer to allocated object
 */
void dctrl_create(void **p, int max_decoders, int cache_size);

/** close and destroy a decoder control object
 * @param p object pointer to free
 */
void dctrl_destroy(void **p);
/**
 * request a video-object id for the given file
 *
 * @param vc pointer to a video-cache object
 * @param p pointer to a decoder-control object
 * @param fn file name to look up
 * @return file-id use with: dctrl_get_info() or dctrl_decode()
 */
unsigned short dctrl_get_id(void *vc, void *p, const char *fn);
/**
 * HTML format debug info and store at most \a n bytes of the message to \a m
 * @param p pointer to a decoder-control object
 * @param m pointer to where result message is stored
 * @param o pointer current offset in m
 * @param s pointer max length of message.
 */
void dctrl_info_html(void *p, char **m, size_t *o, size_t *s, int tbl);
/**
 * request VInfo video-info for given decoder-object
 * @param p  pointer to a decoder-control object
 * @param id id of the decoder
 * @param i returned data
 * @return 0 on success, -1 otherwise
 */
int dctrl_get_info(void *p, unsigned short id, VInfo *i);
/**
 * set new scaling factors and return updated VInfo
 * @param p  pointer to a decoder-control object
 * @param id id of the decoder
 * @param w width or -1 to use aspect-ratio and height
 * @param h height or -1 to use aspect-ratio and width (if both w and h are -1 - no scaling is performed)
 * @param i optional - if not NULL \ref dctrl_get_info is called to fill in the data
 * @return 0 on success, -1 otherwise
 */
int dctrl_get_info_scale(void *p, unsigned short id, VInfo *i, int w, int h, int fmt);

/**
 * used by the frame-cache to decode a frame
 */
int dctrl_decode(void *p, unsigned short vid, int64_t frame, uint8_t *b, int w, int h, int fmt);

/**
 */
void dctrl_cache_clear(void *vc, void *p, int f, int id);
#endif
