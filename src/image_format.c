/*
   This file is part of harvid

   Copyright (C) 2008-2014 Robin Gareus <robin@gareus.org>

   This file contains some GPL source from vgrabbj by
	 Jens Gecius <devel@gecius.de>

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
#ifdef HAVE_WINDOWS
#include <windows.h>
#define HAVE_BOOLEAN 1
#endif

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <png.h>

#include <dlog.h>
#include <vinfo.h> // harvid.h
#include "enums.h"

#define JPEG_QUALITY 75

static int write_jpeg(VInfo *ji, uint8_t *buffer, int quality, FILE *x) {
  uint8_t *line;
  int n, y = 0, i, line_width;

  struct jpeg_compress_struct cjpeg;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_ptr[1];

  line = malloc(ji->out_width * 3);
  if (!line) {
    dlog(DLOG_CRIT, "IMF: OUT OF MEMORY, Exiting...\n");
    exit(1);
  }
  cjpeg.err = jpeg_std_error(&jerr);
  jpeg_create_compress (&cjpeg);
  cjpeg.image_width  = ji->out_width;
  cjpeg.image_height = ji->out_height;
  cjpeg.input_components = 3;
  //cjpeg.smoothing_factor = 0; // 0..100
  cjpeg.in_color_space = JCS_RGB;

  jpeg_set_defaults (&cjpeg);
  jpeg_set_quality (&cjpeg, quality, TRUE);
  cjpeg.dct_method = quality > 90? JDCT_DEFAULT : JDCT_FASTEST;

  jpeg_simple_progression(&cjpeg);

  jpeg_stdio_dest (&cjpeg, x);
  jpeg_start_compress (&cjpeg, TRUE);
  row_ptr[0] = line;
  line_width = ji->out_width * 3;
  n = 0;

  for (y = 0; y < ji->out_height; y++)
    {
      for (i = 0; i< line_width; i += 3)
	{
	  line[i]   = buffer[n];
	  line[i+1] = buffer[n+1];
	  line[i+2] = buffer[n+2];
	  n += 3;
	}
      jpeg_write_scanlines (&cjpeg, row_ptr, 1);
    }
  jpeg_finish_compress (&cjpeg);
  jpeg_destroy_compress (&cjpeg);
  free(line);
  return(0);
}

static int write_png(VInfo *ji, uint8_t *image, FILE *x) {
  register int y;
  png_bytep rowpointers[ji->out_height];
  png_infop info_ptr;
  png_structp png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
						 NULL, NULL, NULL);

  if (!png_ptr)
    return(1);
  info_ptr = png_create_info_struct (png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    return(1);
  }
  if (setjmp(png_jmpbuf(png_ptr))) {
  /* If we get here, we had a problem reading the file */
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return (1);
  }

  png_init_io (png_ptr, x);
  png_set_IHDR (png_ptr, info_ptr, ji->out_width, ji->out_height,
		8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info (png_ptr, info_ptr);
  for (y = 0; y < ji->out_height; y++) {
    rowpointers[y] = image + y*ji->out_width*3;
  }
  png_write_image(png_ptr, rowpointers);
  png_write_end (png_ptr, info_ptr);
  png_destroy_write_struct (&png_ptr, &info_ptr);
  return(0);
}

static int write_ppm(VInfo *ji, uint8_t *image, FILE *x) {

  fprintf(x, "P6\n%d %d\n255\n", ji->out_width, ji->out_height);
  fwrite(image, ji->out_height, 3*ji->out_width, x);

  return(0);
}

static FILE *open_outfile(char *filename) {
  if (!strcmp(filename, "-")) return stdout;
  return fopen(filename, "w+");
}

size_t format_image(uint8_t **out, int render_fmt, int misc_int, VInfo *ji, uint8_t *buf) {
#ifdef HAVE_WINDOWS
  char tfn[64] = "";
#endif
#ifdef __USE_XOPEN2K8
  size_t rs = 0;
  FILE *x = open_memstream((char**) out, &rs);
#else
  FILE *x = tmpfile();
#endif
#ifdef HAVE_WINDOWS
  if (!x) {
    // wine and ancient version of windows don't support tmpfile()
    // srand is per thread :(
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec * tv.tv_usec * 100000);
    snprintf(tfn, sizeof(tfn), "harvid.tmp.%d", rand());
    x = fopen(tfn, "w+b"); // we should really use open(tfn, O_RDWR | O_CREAT | O_EXCL, 0600); and fdopen
  }
#endif
  if (!x) {
    dlog(LOG_ERR, "IMF: tmpfile() creation failed.\n");
    return(0);
  }

  switch (render_fmt) {
    case 1:
      {
        if (misc_int < 5 || misc_int > 100)
          misc_int = JPEG_QUALITY;
      if (write_jpeg(ji, buf, misc_int, x))
        dlog(LOG_ERR, "IMF: Could not write jpeg\n");
      break;
      }
    case 2:
      if (write_png(ji, buf, x))
        dlog(LOG_ERR, "IMF: Could not write png\n");
      break;
    case 3:
      if (write_ppm(ji, buf, x))
        dlog(LOG_ERR, "IMF: Could not write ppm\n");
      break;
    default:
        dlog(LOG_ERR, "IMF: Unknown outformat %d\n", render_fmt);
        fclose(x);
        return 0;
      break;
  }
#ifdef __USE_XOPEN2K8
  fclose(x);
  return rs;
#elif defined HAVE_WINDOWS
  if (strlen(tfn) > 0) {
    fclose(x);
    x = fopen(tfn, "rb");
  } else {
    fflush(x);
  }
#else
  fflush(x);
#endif
  /* re-read image from tmp-file */
  fseek (x , 0 , SEEK_END);
  long int rsize = ftell (x);
  rewind(x);
  if (fseek(x, 0L, SEEK_SET) < 0) {
    dlog(LOG_WARNING, "IMF: fseek failed\n");
  }
  fflush(x);
  *out = (uint8_t*) malloc(rsize*sizeof(uint8_t));
  if (fread(*out, sizeof(char), rsize, x) != rsize) {
    dlog(LOG_WARNING, "IMF: short read. - possibly incomplete image\n");
  }
  fclose(x);
#ifdef HAVE_WINDOWS
  if (strlen(tfn) > 0) {
    unlink(tfn);
  }
#endif
  return (rsize);
}

void write_image(char *file_name, int render_fmt, VInfo *ji, uint8_t *buf) {
  FILE *x;
  if ((x = open_outfile(file_name))) {
    switch (render_fmt) {
      case FMT_JPG:
	if (write_jpeg(ji, buf, JPEG_QUALITY, x))
	  dlog(LOG_ERR, "IMF: Could not write jpeg: %s\n", file_name);
	break;
      case FMT_PNG:
	if (write_png(ji, buf, x))
	  dlog(LOG_ERR, "IMF: Could not write png: %s\n", file_name);
	break;
      case FMT_PPM:
	if (write_ppm(ji, buf, x))
	  dlog(LOG_ERR, "IMF: Could not write ppm: %s\n", file_name);
	break;
      default:
	dlog(LOG_ERR, "IMF: Unknown outformat %d\n", render_fmt);
	break;
    }
    if (strcmp(file_name, "-")) fclose(x);
    dlog(LOG_INFO, "IMF: Outputfile %s closed\n", file_name);
  }
  else
    dlog(LOG_ERR, "IMF: Could not open outfile: %s\n", file_name);
  return;
}

// vim:sw=2 sts=2 ts=8 et:
