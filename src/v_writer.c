/*
   This file is part of harvid

   Copyright (C) 2008-2013 Robin Gareus <robin@gareus.org>

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

#include "jv.h"

extern int want_quiet;
extern int want_verbose;

int cfg_jpeg_quality = 75;

static int write_jpeg(JVINFO *ji, uint8_t *buffer, FILE *x) {
  uint8_t *line;
  int n, y=0, i, line_width;

  struct jpeg_compress_struct cjpeg;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_ptr[1];

  line=malloc(ji->out_width * 3);
  if (!line) {
    fprintf(stderr, "OUT OF MEMORY, Exiting...\n"); exit(1);
  }
  cjpeg.err = jpeg_std_error(&jerr);
  jpeg_create_compress (&cjpeg);
  cjpeg.image_width = ji->out_width;
  cjpeg.image_height= ji->out_height;
  cjpeg.input_components = 3;
  cjpeg.in_color_space = JCS_RGB;

  jpeg_set_defaults (&cjpeg);
  jpeg_set_quality (&cjpeg, cfg_jpeg_quality, TRUE); // TODO: use config var
  cjpeg.dct_method = JDCT_FASTEST;

  jpeg_stdio_dest (&cjpeg, x);
  jpeg_start_compress (&cjpeg, TRUE);
  row_ptr[0]=line;
  line_width=ji->out_width * 3;
  n=0;

  for (y = 0; y < ji->out_height; y++)
    {
      for (i = 0; i< line_width; i+=3)
	{
	  line[i]   = buffer[n];
	  line[i+1] = buffer[n+1];
	  line[i+2] = buffer[n+2];
	  n+=3;
	}
      jpeg_write_scanlines (&cjpeg, row_ptr, 1);
    }
  jpeg_finish_compress (&cjpeg);
  jpeg_destroy_compress (&cjpeg);
  free(line);
  return(0);
}

static int write_png(JVINFO *ji, uint8_t *image, FILE *x) {
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
  for (y = 0; y < ji->out_height; y++)
    {
      rowpointers[y] = image + y*ji->out_width*3;
    }
  png_write_image(png_ptr, rowpointers);
  png_write_end (png_ptr, info_ptr);
  png_destroy_write_struct (&png_ptr, &info_ptr);
  return(0);
}

static int write_ppm(JVINFO *ji, uint8_t *image, FILE *x) {

  fprintf(x,"P6\n%d %d\n255\n",ji->out_width,ji->out_height);
  fwrite(image,ji->out_height,3*ji->out_width,x);

  return(0);
}

static FILE *open_outfile(char *filename) {
  FILE *x;
  int err_count=0;
  if (!strcmp(filename,"-")) return stdout;
#if 0
  if (atoi(filename) >0 ) return fdopen(atoi(filename), "w+");
  if (!strcmp("%",filename))  return tmpfile();
#endif
  while (!(x = fopen(filename, "w+")) && (++err_count < 200) )
#ifndef HAVE_WINDOWS
    usleep(25000);
#else
	  Sleep(25);
#endif
  if (!x) return 0;
  return x;
}

/* Function to get an image, called by main */
long int format_image(uint8_t **out, JVARGS *ja, JVINFO *ji, uint8_t *buf) {
#ifndef HAVE_WINDOWS
  FILE *x = tmpfile();
#else
  char tfn[L_tmpnam]; // = "C:\\icsd.tmp"
	tmpnam(tfn);
  FILE *x = fopen(tfn, "w+b");
#endif
  if (x) {
    switch (ja->render_fmt) {
      case 1:
	while (write_jpeg(ji, buf, x))
	  fprintf(stderr, "Could not write tmpfile\n");
	break;
      case 2:
	while (write_png(ji, buf, x))
	  fprintf(stderr, "Could not write tmpfile\n");
	break;
      case 3:
	while (write_ppm(ji, buf, x))
	  fprintf(stderr, "Could not write tmpfile\n");
	break;
      default:
	fprintf(stderr, "Unknown outformat %d\n", ja->render_fmt);
	break;
    }
#ifdef HAVE_WINDOWS
		fclose(x);
		x = fopen(tfn, "rb");
#endif
		fflush(x);
    fseek (x , 0 , SEEK_END);
    long int rsize = ftell (x);
    rewind(x);
		if (fseek(x, 0L, SEEK_SET) < 0) {
			fprintf(stderr,"debug: seek failed!!!!!!!!!!!!!\n");
		}
		fflush(x);
    *out = (uint8_t*) malloc(rsize*sizeof(uint8_t));
    if (fread(*out, sizeof(char), rsize, x) !=rsize) {
      fprintf(stderr,"short read. - possibly incomplete image\n");
    }
    fclose(x);
#ifdef HAVE_WINDOWS
		unlink(tfn);
#endif
    return (rsize);
  } else {
      fprintf(stderr,"critical error: tmpfile() failed.\n");
	}
  return(0);
}

/* Function to write an image to file, called by main */
void write_image(JVARGS *ja, JVINFO *ji, uint8_t *buf) {
  FILE *x;
  if ( (x = open_outfile(ja->file_name)) ) {
    switch (ja->render_fmt) {
      case 1:
	while (write_jpeg(ji, buf, x))
	  fprintf(stderr, "Could not write outputfile %s\n", ja->file_name);
	break;
      case 2:
	while (write_png(ji, buf, x))
	  fprintf(stderr, "Could not write outputfile %s\n", ja->file_name);
	break;
      case 3:
	while (write_ppm(ji, buf, x))
	  fprintf(stderr, "Could not write outputfile %s\n", ja->file_name);
	break;
      default:
	fprintf(stderr, "Unknown outformat %d\n", ja->render_fmt);
	break;
    }
    if (strcmp(ja->file_name,"-")) fclose(x);
    if (want_verbose)
    	fprintf(stderr, "Outputfile %s closed\n", ja->file_name);
  }
  else
    fprintf(stderr, "Could not open outfile %s\n", ja->file_name);
  return;
}
