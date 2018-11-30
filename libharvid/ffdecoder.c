/*
   This file is part of harvid

   Copyright (C) 2007-2013 Robin Gareus <robin@gareus.org>

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
#include <stdio.h>
#include <stdint.h>     /* uint8_t */
#include <stdlib.h>     /* calloc et al.*/
#include <string.h>     /* memset */
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>

#include "vinfo.h"
#include "ffdecoder.h"

#include "ffcompat.h"
#include <libswscale/swscale.h>

#ifndef MAX
#define MAX(A,B) ( ( (A) > (B) ) ? (A) : (B) )
#endif

/* ffmpeg source */
typedef struct {
  /* file specific decoder settings */
  int   want_ignstart; //< set before calling ff_open_movie()
  int   want_genpts;
  /* Video File Info */
  int   movie_width;  ///< original file geometry
  int   movie_height; ///< original file geometry
  int   out_width;  ///< aspect scaled geometry
  int   out_height; ///< aspect scaled geometry

  double duration;
  double framerate;
  TimecodeRate tc;
  double file_frame_offset;
  long   frames;
  char  *current_file;
  /* helper variables */
  int64_t tpf;
  int64_t avprev;
  int64_t stream_pts_offset;
  /* */
  uint8_t *internal_buffer; //< if !NULL this buffer is free()d on destroy
  uint8_t *buffer;
  int   buf_width;  ///< current geometry for allocated buffer
  int   buf_height; ///< current geometry for allocated buffer
  int   videoStream;
  int   render_fmt;  //< pFrame/buffer output format (RGB24)
  /* ffmpeg internals*/
  AVPacket          packet;
  AVFormatContext   *pFormatCtx;
  AVCodecContext    *pCodecCtx;
  AVFrame           *pFrame;
  AVFrame           *pFrameFMT;
  struct SwsContext *pSWSCtx;
} ffst;

/* Option flags and global variables */
extern int want_quiet;
extern int want_verbose;

static pthread_mutex_t avcodec_lock;
static const AVRational c1_Q = { 1, 1 };

//#define SCALE_UP  ///< positive pixel-aspect scales up X axis - else positive pixel-aspect scales down Y-Axis.

//--------------------------------------------
// Manage video file
//--------------------------------------------

int ff_picture_bytesize(int render_fmt, int w, int h) {
  const int bs = avpicture_get_size(render_fmt, w, h);
  if (bs < 0) return 0;
  return bs;
}

static int ff_getbuffersize(void *ptr, size_t *s) {
  ffst *ff = (ffst*)ptr;
  const int ps = ff_picture_bytesize(ff->render_fmt, ff->out_width, ff->out_height);
  if (s) *s = ps;
  return ps;
}

static void render_empty_frame(ffst *ff, uint8_t* buf, int w, int h, int xoff, int ys) {
  switch (ff->render_fmt) {
    case AV_PIX_FMT_UYVY422:
      {
	int i;
	for (i = 0; i < w*h*2; i += 2) {
	 buf[i] = 0x00; buf[i+1] = 0x80;
	}
      }
      break;
    case AV_PIX_FMT_YUYV422:
      {
	int i;
	for (i = 0; i < w*h*2; i += 2) {
	 buf[i] = 0x80; buf[i+1] = 0x00;
	}
      }
      break;
    case AV_PIX_FMT_YUV420P:
      {
	size_t Ylen = w * h;
	memset(buf, 0, Ylen);
	memset(buf+Ylen, 0x80, Ylen/2);
      }
      break;
    case AV_PIX_FMT_YUV440P:
      {
	size_t Ylen = w * h;
	memset(buf, 0, Ylen);
	memset(buf+Ylen, 0x80, Ylen);
      }
      break;
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_ARGB:
      memset(buf, 0, ff_getbuffersize(ff, NULL));
      break;
    default:
      if (!want_quiet)
	fprintf(stderr, "render_empty_frame() with unknown render format\n");
      break;
  }
#if 1 // draw cross
  int x,y;
  switch (ff->render_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV440P:
      for (x = 0, y = 0; x < w-1; x++, y = h * x / w) {
	int off = (x + w * y);
	buf[off]=127; buf[off+1]=127;
	off = (x + w * (h - y - 1));
	buf[off]=127; buf[off+1]=127;
      }
      break;
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_UYVY422:
      for (x = 0, y = 0; x < w-1; x++, y = h * x / w) {
	int off = (x + w * y) * 2;
	buf[off] = 127; buf[off+1] = 127;
	off = (x + w * (h - y - 1)) * 2;
	buf[off] = 127; buf[off+1] = 127;
      }
      break;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
      for (x = 0, y = 0; x < w-1; x++, y = h * x / w) {
	int off = 3 * (x + w * y);
	buf[off]=255; buf[off+1]=255; buf[off+2]=255;
	off = 3 * (x + w * (h - y - 1));
	buf[off]=255; buf[off+1]=255; buf[off+2]=255;
      }
      break;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_ARGB:
      {
      const int O = (ff->render_fmt == AV_PIX_FMT_ARGB) ? 1 : 0;
      for (x = 0, y = 0; x < w-1; x++, y = h * x / w) {
	int off = 4 * (x + w * y) + O;
	buf[off]=255; buf[off+1]=255; buf[off+2]=255;
	off = 4 * (x + w * (h - y - 1)) + O;
	buf[off]=255; buf[off+1]=255; buf[off+2]=255;
      }
      }
      break;
    default:
      break;
  }
#endif
}

static double ff_get_aspectratio(void *ptr) {
  ffst *ff = (ffst*)ptr;
  double aspect_ratio;
  if ( ff->pCodecCtx->sample_aspect_ratio.num == 0 || ff->pCodecCtx->sample_aspect_ratio.den == 0)
    aspect_ratio = 0;
  else
    aspect_ratio = av_q2d(ff->pCodecCtx->sample_aspect_ratio)
                   * (double)ff->pCodecCtx->width / (double)ff->pCodecCtx->height;
  if (aspect_ratio <= 0.0)
    aspect_ratio = (double)ff->pCodecCtx->width / (double)ff->pCodecCtx->height;
  return (aspect_ratio);
}

static void ff_caononicalize_size2(void *ptr, int *w, int *h) {
  ffst *ff = (ffst*)ptr;
  double aspect_ratio = ff_get_aspectratio(ptr);
  if (!w || !h) return;

  if ((*h) < 16 && (*w) > 15) (*h) = (int) floorf((float)(*w)/aspect_ratio);
  else if ((*h) > 15  && (*w) < 16) (*w) = (int) floorf((float)(*h)*aspect_ratio);

  if ((*w) < 16 || (*h) < 16) {
#ifdef SCALE_UP
    (*w) = (int) floor((double)ff->pCodecCtx->height * aspect_ratio);
    (*h) = ff->pCodecCtx->height;
#else
    (*w) = ff->pCodecCtx->width ;
    (*h) = (int) floor((double)ff->pCodecCtx->width / aspect_ratio);
#endif
  }
}

static void ff_caononical_size(void *ptr) {
  ffst *ff = (ffst*)ptr;
  ff_caononicalize_size2(ptr, &ff->out_width, &ff->out_height);
}

static void ff_init_moviebuffer(void *ptr) {
  size_t numBytes = 0;
  ffst *ff = (ffst*)ptr;

  ff_caononical_size(ptr);

  if (ff->buf_width == ff->out_width && ff->buf_height == ff->out_height) {
    return;
  } else if (want_verbose) {
    fprintf(stdout, "ff_init_moviebuffer %dx%d vs %dx%d\n", ff->buf_width, ff->buf_height, ff->out_width, ff->out_height);
  }

  if (ff->internal_buffer) free(ff->internal_buffer);
  ff_getbuffersize(ff, &numBytes);
  assert(numBytes > 0);
  ff->internal_buffer = (uint8_t *) calloc(numBytes, sizeof(uint8_t));
  ff->buffer = ff->internal_buffer;
  ff->buf_width = ff->out_width;
  ff->buf_height = ff->out_height;
  if (!ff->buffer) {
#ifdef _WIN32
    fprintf(stderr, "out of memory (trying to allocate %lu bytes)\n", (long unsigned) numBytes);
#else
    fprintf(stderr, "out of memory (trying to allocate %zu bytes)\n", numBytes);
#endif
    exit(1);
  }
  assert(ff->pFrameFMT);
  avpicture_fill((AVPicture *)ff->pFrameFMT, ff->buffer, ff->render_fmt, ff->out_width, ff->out_height);
}

void ff_initialize (void) {
  if (want_verbose) fprintf(stdout, "FFMPEG: registering codecs.\n");
  av_register_all();
  avcodec_register_all();
  pthread_mutex_init(&avcodec_lock, NULL);

  if(want_quiet) av_log_set_level(AV_LOG_QUIET);
  else if (want_verbose) av_log_set_level(AV_LOG_VERBOSE);
  else av_log_set_level(AV_LOG_ERROR);
}

void ff_cleanup (void) {
  pthread_mutex_destroy(&avcodec_lock);
}

int ff_close_movie(void *ptr) {
  ffst *ff = (ffst*)ptr;
  if(ff->current_file) free(ff->current_file);
  ff->current_file = NULL;

  if (!ff->pFrameFMT) return(-1);
  if (ff->out_width < 0 || ff->out_height < 0) {
    ff->out_width = ff->movie_width;
    ff->out_height = ff->movie_height;
  }
  ff_set_bufferptr(ff, ff->internal_buffer); // restore allocated movie-buffer..
  if (ff->internal_buffer) free(ff->internal_buffer); // done in pFrameFMT?
  if (ff->pFrameFMT) av_free(ff->pFrameFMT);
  if (ff->pFrame) av_free(ff->pFrame);
  ff->buffer = NULL;ff->pFrameFMT = ff->pFrame = NULL;
  pthread_mutex_lock(&avcodec_lock);
  avcodec_close(ff->pCodecCtx);
  avformat_close_input(&ff->pFormatCtx);
  pthread_mutex_unlock(&avcodec_lock);
  if (ff->pSWSCtx) sws_freeContext(ff->pSWSCtx);
  return (0);
}

static void ff_set_framerate(ffst *ff) {
  AVStream *av_stream;
  av_stream = ff->pFormatCtx->streams[ff->videoStream];

  ff->framerate = 0;
  ff->tc.num = 0;
  ff->tc.den = 1;

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(55, 0, 100) // 9cf788eca8ba (merge a75f01d7e0)
  {
    AVRational fr = av_stream->r_frame_rate;
    if (fr.den > 0 && fr.num > 0) {
      ff->framerate = av_q2d (av_stream->r_frame_rate);
      ff->tc.num = fr.num;
      ff->tc.den = fr.den;
    }
  }
#else
  {
    AVRational fr = av_stream_get_r_frame_rate (av_stream);
    if (fr.den > 0 && fr.num > 0) {
      ff->framerate = av_q2d (fr);
      ff->tc.num = fr.num;
      ff->tc.den = fr.den;
    }
  }
#endif
  if (ff->framerate < 1 || ff->framerate > 1000) {
    AVRational fr = av_stream->avg_frame_rate;
    if (fr.den > 0 && fr.num > 0) {
      ff->framerate = av_q2d (fr);
      ff->tc.num = fr.num;
      ff->tc.den = fr.den;
    }
  }
  if (ff->framerate < 1 || ff->framerate > 1000) {
    AVRational fr = av_stream->time_base;
    if (fr.den > 0 && fr.num > 0) {
      ff->framerate = 1.0 / av_q2d (fr);
      ff->tc.num = fr.den;
      ff->tc.den = fr.num;
    }
  }
  if (ff->framerate < 1 || ff->framerate > 1000) {
    if (!want_quiet)
      fprintf(stderr, "WARNING: cannot determine video-frame rate, using 25fps.\n");
    ff->framerate = 25;
    ff->tc.num = 25;
    ff->tc.den = 1;
  }

  ff->tc.drop = 0;
  if (floor(ff->framerate * 100.0) == 2997)
    ff->tc.drop = 1;
}

int ff_open_movie(void *ptr, char *file_name, int render_fmt) {
  int i;
  AVCodec *pCodec;
  ffst *ff = (ffst*) ptr;

  if (ff->pFrameFMT) {
    if (ff->current_file && !strcmp(file_name, ff->current_file)) return(0);
    /* close currently open movie */
    if (!want_quiet)
      fprintf(stderr, "replacing current video file buffer\n");
    ff_close_movie(ff);
  }

  // initialize values
  ff->pFormatCtx = NULL;
  ff->pFrameFMT = NULL;
  ff->movie_width  = 320;
  ff->movie_height = 180;
  ff->buf_width = ff->buf_height = 0;
  ff->movie_height = 180;
  ff->framerate = ff->duration = ff->frames = 1;
  ff->file_frame_offset = 0.0;
  ff->videoStream = -1;
  ff->tpf = 1;
  ff->avprev = -1;
  ff->stream_pts_offset = AV_NOPTS_VALUE;
  ff->render_fmt = render_fmt;

  /* Open video file */
  if(avformat_open_input(&ff->pFormatCtx, file_name, NULL, NULL) <0)
  {
    if (!want_quiet)
      fprintf(stderr, "Cannot open video file %s\n", file_name);
    return (-1);
  }

  pthread_mutex_lock(&avcodec_lock);
  /* Retrieve stream information */
  if(avformat_find_stream_info(ff->pFormatCtx, NULL) < 0) {
    if (!want_quiet)
      fprintf(stderr, "Cannot find stream information in file %s\n", file_name);
    avformat_close_input(&ff->pFormatCtx);
    pthread_mutex_unlock(&avcodec_lock);
    return (-1);
  }
  pthread_mutex_unlock(&avcodec_lock);

  if (want_verbose) av_dump_format(ff->pFormatCtx, 0, file_name, 0);

  /* Find the first video stream */
  for(i = 0; i < ff->pFormatCtx->nb_streams; i++)
#if LIBAVFORMAT_BUILD > 0x350000
    if(ff->pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#elif LIBAVFORMAT_BUILD > 4629
    if(ff->pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO)
#else
    if(ff->pFormatCtx->streams[i]->codec.codec_type == CODEC_TYPE_VIDEO)
#endif
    {
      ff->videoStream = i;
      break;
    }

  if(ff->videoStream == -1) {
    if (!want_quiet)
      fprintf(stderr, "Cannot find a video stream in file %s\n", file_name);
    avformat_close_input(&ff->pFormatCtx);
    return (-1);
  }

  ff_set_framerate(ff);

  {
  AVStream *avs = ff->pFormatCtx->streams[ff->videoStream];

#if 0 // DEBUG duration
  printf("DURATION frames from AVstream: %"PRIi64"\n", avs->nb_frames);
  printf("DURATION duration from FormatContext: %.2f\n", ff->pFormatCtx->duration * ff->framerate / AV_TIME_BASE);
#endif

  if (avs->nb_frames > 0) {
    ff->frames = avs->nb_frames;
    ff->duration = ff->frames / ff->framerate;
  } else {
    ff->duration = ff->pFormatCtx->duration / (double)AV_TIME_BASE;
    ff->frames = ff->pFormatCtx->duration * ff->framerate / AV_TIME_BASE;
  }

  const AVRational fr_Q = { ff->tc.den, ff->tc.num };
  ff->tpf = av_rescale_q (1, fr_Q, avs->time_base);
  }

  ff->file_frame_offset = ff->framerate*((double) ff->pFormatCtx->start_time/ (double) AV_TIME_BASE);

  if (want_verbose) {
    fprintf(stdout, "frame rate: %g\n", ff->framerate);
    fprintf(stdout, "length in seconds: %g\n", ff->duration);
    fprintf(stdout, "total frames: %ld\n", ff->frames);
    fprintf(stdout, "start offset: %.0f [frames]\n", ff->file_frame_offset);
  }

  // Get a pointer to the codec context for the video stream
#if LIBAVFORMAT_BUILD > 4629
  ff->pCodecCtx = ff->pFormatCtx->streams[ff->videoStream]->codec;
#else
  ff->pCodecCtx = &(ff->pFormatCtx->streams[ff->videoStream]->codec);
#endif

// FIXME: don't scale here - announce aspect ratio
// out_width/height remains in aspect 1:1
#ifdef SCALE_UP
  ff->movie_width = (int) floor((double)ff->pCodecCtx->height * ff_get_aspectratio(ff));
  ff->movie_height = ff->pCodecCtx->height;
#else
  ff->movie_width = ff->pCodecCtx->width;
  ff->movie_height = (int) floor((double)ff->pCodecCtx->width / ff_get_aspectratio(ff));
#endif

  // somewhere around LIBAVFORMAT_BUILD  4630
#ifdef AVFMT_FLAG_GENPTS
  if (ff->want_genpts) {
    ff->pFormatCtx->flags |= AVFMT_FLAG_GENPTS;
//  ff->pFormatCtx->flags |= AVFMT_FLAG_IGNIDX;
  }
#endif

  if (want_verbose)
    fprintf(stdout, "movie size:  %ix%i px\n", ff->movie_width, ff->movie_height);

  // Find the decoder for the video stream
  pCodec = avcodec_find_decoder(ff->pCodecCtx->codec_id);
  if(pCodec == NULL) {
    if (!want_quiet)
      fprintf(stderr, "Cannot find a codec for file: %s\n", file_name);
    avformat_close_input(&ff->pFormatCtx);
    return(-1);
  }

  // Open codec
  pthread_mutex_lock(&avcodec_lock);
  if(avcodec_open2(ff->pCodecCtx, pCodec, NULL) < 0) {
    if (!want_quiet)
      fprintf(stderr, "Cannot open the codec for file %s\n", file_name);
    pthread_mutex_unlock(&avcodec_lock);
    avformat_close_input(&ff->pFormatCtx);
    return(-1);
  }
  pthread_mutex_unlock(&avcodec_lock);

  if (!(ff->pFrame = av_frame_alloc())) {
    if (!want_quiet)
      fprintf(stderr, "Cannot allocate video frame buffer\n");
    avcodec_close(ff->pCodecCtx);
    avformat_close_input(&ff->pFormatCtx);
    return(-1);
  }

  if (!(ff->pFrameFMT = av_frame_alloc())) {
    if (!want_quiet)
      fprintf(stderr, "Cannot allocate display frame buffer\n");
    av_free(ff->pFrame);
    avcodec_close(ff->pCodecCtx);
    avformat_close_input(&ff->pFormatCtx);
    return(-1);
  }

  ff->out_width = ff->out_height = -1;

  ff->current_file = strdup(file_name);
  return(0);
}

static uint64_t parse_pts_from_frame (AVFrame *f) {
  uint64_t pts = AV_NOPTS_VALUE;
  static uint8_t pts_warn = 0; // should be per decoder

  pts = AV_NOPTS_VALUE;

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(51, 49, 100)
  if (pts == AV_NOPTS_VALUE) {
    pts = av_frame_get_best_effort_timestamp (f);
    if (pts != AV_NOPTS_VALUE) {
      if (!(pts_warn & 1) && want_verbose)
	fprintf(stderr, "PTS: Best effort.\n");
      pts_warn |= 1;
    }
  }
#else
#warning building with libavutil < 51.49.100 is highly discouraged
#endif

  if (pts == AV_NOPTS_VALUE) {
    pts = f->pkt_pts;
    if (pts != AV_NOPTS_VALUE) {
      if (!(pts_warn & 2) && want_verbose)
	fprintf(stderr, "Used PTS from packet instead frame's PTS.\n");
      pts_warn |= 2;
    }
  }

  if (pts == AV_NOPTS_VALUE) {
    pts = f->pts; // sadly bogus with many codecs :(
    if (pts != AV_NOPTS_VALUE) {
      if (!(pts_warn & 8) && want_verbose)
	fprintf(stderr, "Used AVFrame assigned pts (instead frame PTS).\n");
      pts_warn |= 8;
    }
  }

  if (pts == AV_NOPTS_VALUE) {
    pts = f->pkt_dts;
    if (pts != AV_NOPTS_VALUE) {
      if (!(pts_warn & 4) && want_verbose)
	fprintf(stderr, "Used decode-timestamp from packet (instead frame PTS).\n");
      pts_warn |= 4;
    }
  }

  return pts;
}

static int my_seek_frame (ffst *ff, AVPacket *packet, int64_t framenumber) {
  AVStream *v_stream;
  int rv = 0;
  int64_t timestamp;

  if (ff->videoStream < 0) return (0);
  v_stream = ff->pFormatCtx->streams[ff->videoStream];

  if (ff->want_ignstart)
    framenumber += (int64_t) rint(ff->framerate * ((double)ff->pFormatCtx->start_time / (double)AV_TIME_BASE));

  if (framenumber < 0 || framenumber >= ff->frames) {
    return -1;
  }

  const AVRational fr_Q = { ff->tc.den, ff->tc.num };
  timestamp = av_rescale_q(framenumber, fr_Q, v_stream->time_base);

  if (ff->avprev == timestamp) {
    return 0;
  }

  if (ff->avprev < 0 || ff->avprev >= timestamp || ((ff->avprev + 32 * ff->tpf) < timestamp)) {
    rv = av_seek_frame(ff->pFormatCtx, ff->videoStream, timestamp, AVSEEK_FLAG_BACKWARD) ;
    if (ff->pCodecCtx->codec->flush) {
      avcodec_flush_buffers(ff->pCodecCtx);
    }
  }

  ff->avprev = -1;

  if (rv < 0) {
    return -1;
  }

  int bailout = 600;
  int decoded = 0;
  while (bailout > 0) {
    int err;
    if ((err = av_read_frame (ff->pFormatCtx, packet)) < 0) {
      if (err != AVERROR_EOF) {
	av_free_packet (packet);
	return -1;
      } else {
	--bailout;
      }
    }
    if(packet->stream_index != ff->videoStream) {
      av_free_packet (packet);
      continue;
    }

    int frameFinished = 0;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 21, 0)
    err = avcodec_decode_video (ff->pCodecCtx, ff->pFrame, &frameFinished, packet->data, packet->size);
#else
    err = avcodec_decode_video2 (ff->pCodecCtx, ff->pFrame, &frameFinished, packet);
#endif
    av_free_packet (packet);

    if (err < 0) {
      return -10;
    }

    if (!frameFinished) {
      --bailout;
      continue;
    }

    int64_t pts = parse_pts_from_frame (ff->pFrame);

    if (pts == AV_NOPTS_VALUE) {
      return -7;
    }

    const int64_t prefuzz = ff->tpf > 10 ? 1 : 0;
    if (pts + prefuzz >= timestamp) {
      if (pts - timestamp < ff->tpf) {
	ff->avprev = pts;
	return 0; // OK
      }
      // Cannot reliably seek to target frame
      if (decoded == 0) {
	if (want_verbose)
	  fprintf(stdout, " PTS mismatch want: %"PRId64" got: %"PRId64" -> re-seek\n", timestamp, pts);
	// re-seek - make a guess, since we don't know the keyframe interval
	rv = av_seek_frame(ff->pFormatCtx, ff->videoStream, MAX(0, timestamp - ff->tpf * 25), AVSEEK_FLAG_BACKWARD) ;
	if (ff->pCodecCtx->codec->flush) {
	  avcodec_flush_buffers(ff->pCodecCtx);
	}
	if (rv < 0) {
	  return -3;
	}
	--bailout;
	++decoded;
	continue;
      }
      if (!want_quiet)
	fprintf(stderr, " PTS mismatch want: %"PRId64" got: %"PRId64" -> fail\n", timestamp, pts); // XXX
      return -2;
    }

    --bailout;
    ++decoded;
  }
  return -5;
}

/**
 * seeks to frame and decodes and scales video frame
 *
 * @arg ptr handle / ff-data structure
 * @arg frame video frame to seek to
 * @arg buf  unused - see ff_get_bufferptr() - soon: optional buffer-pointer to copy data into
 * @arg w  unused - target width -> out_height parameter when opening file
 * @arg h  unused - target height -> out_height parameter when opening file
 * @arg xoff unused - soon: x-offset of this frame to target buffer
 * @arg xw unused -  really unused
 * @arg ys unused -  soon: y-stride (aka width of container)
 */
int ff_render(void *ptr, unsigned long frame,
    uint8_t* buf, int w, int h, int xoff, int xw, int ys) {
  ffst *ff = (ffst*) ptr;

  if (ff->buffer == ff->internal_buffer && (ff->buf_width <= 0 || ff->buf_height <= 0)) {
    ff_init_moviebuffer(ff);
  }

  if (ff->pFrameFMT && ff->pFormatCtx && !my_seek_frame(ff, &ff->packet, frame)) {
    ff->pSWSCtx = sws_getCachedContext(ff->pSWSCtx, ff->pCodecCtx->width, ff->pCodecCtx->height, ff->pCodecCtx->pix_fmt, ff->out_width, ff->out_height, ff->render_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    sws_scale(ff->pSWSCtx, (const uint8_t * const*) ff->pFrame->data, ff->pFrame->linesize, 0, ff->pCodecCtx->height, ff->pFrameFMT->data, ff->pFrameFMT->linesize);
    return 0;
  }

  if (ff->pFrameFMT && !want_quiet) {
    fprintf( stderr, "frame seek unsucessful (frame: %lu).\n", frame);
  }

  render_empty_frame(ff, buf, w, h, xoff, ys);
  return -1;
}

void ff_get_info(void *ptr, VInfo *i) {
  ffst *ff = (ffst*) ptr;
  if (!i) return;
  // TODO check if move is open.. (not needed, dctrl prevents that)
  i->movie_width = ff->movie_width;
  i->movie_height = ff->movie_height;
  i->movie_aspect = ff_get_aspectratio(ptr);
  i->out_width = ff->out_width;
  i->out_height = ff->out_height;
  i->file_frame_offset = ff->file_frame_offset;
  if (ff->out_height > 0 && ff->out_width > 0)
    ff_getbuffersize(ptr, &i->buffersize);
  else
    i->buffersize = 0;
  i->frames = ff->frames;

  memcpy(&i->framerate, &ff->tc, sizeof(TimecodeRate));
}

void ff_get_info_canonical(void *ptr, VInfo *i, int w, int h) {
  ffst *ff = (ffst*) ptr;
  if (!i) return;
  ff_get_info(ptr, i);
  i->out_width = w;
  i->out_height = h;
  ff_caononicalize_size2(ptr, &i->out_width, &i->out_height);
  i->buffersize = ff_picture_bytesize(ff->render_fmt, i->out_width, i->out_height);
}

void ff_create(void **ff) {
  (*((ffst**)ff)) = (ffst*) calloc(1, sizeof(ffst));
  (*((ffst**)ff))->render_fmt = AV_PIX_FMT_RGB24;
  (*((ffst**)ff))->want_ignstart = 0;
  (*((ffst**)ff))->want_genpts = 0;
  (*((ffst**)ff))->packet.data = NULL;
}

void ff_destroy(void **ff) {
  ff_close_movie(*((ffst**)ff));
  free(*((ffst**)ff));
  *ff = NULL;
}

// buf needs to point to an allocated area of ff->out_width, ff->out_height.
// ffmpeg will directly decode/scale into this buffer.
// if it's NULL an internal buffer will be used.
uint8_t *ff_set_bufferptr(void *ptr, uint8_t *buf) {
  ffst *ff = (ffst*) ptr;
  if (buf)
    ff->buffer = buf;
  else
    ff->buffer = ff->internal_buffer;
  avpicture_fill((AVPicture *)ff->pFrameFMT, ff->buffer, ff->render_fmt, ff->out_width, ff->out_height);
  return (NULL); // return prev. buffer?
}

uint8_t *ff_get_bufferptr(void *ptr) {
  ffst *ff = (ffst*) ptr;
  return ff->buffer;
}

void ff_resize(void *ptr, int w, int h, uint8_t *buf, VInfo *i) {
  ffst *ff = (ffst*) ptr;
  ff->out_width = w;
  ff->out_height = h;
  if (!buf)
    ff_caononical_size(ff);
  else
    ff_set_bufferptr(ptr, buf);
  if (i) ff_get_info(ptr, i);
}

const char * ff_fmt_to_text(int fmt) {
  switch (fmt) {
    case AV_PIX_FMT_NONE:
      return "-";
    case AV_PIX_FMT_BGR24:
      return "BGR24";
    case AV_PIX_FMT_RGB24:
      return "RGB24";
    case AV_PIX_FMT_RGBA:
      return "RGBA";
    case AV_PIX_FMT_BGRA:
      return "BGRA";
    case AV_PIX_FMT_ARGB:
      return "ARGB";
    case AV_PIX_FMT_YUV420P:
      return "YUV420P";
    case AV_PIX_FMT_YUYV422:
      return "YUYV422";
    case AV_PIX_FMT_UYVY422:
      return "UYVY422";
    case AV_PIX_FMT_YUV440P:
      return "YUV440P";
    default:
      return "?";
  }
}

/* vi:set ts=8 sts=2 sw=2: */
