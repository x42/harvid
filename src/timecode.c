/*
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "timecode.h"

#define TCtoDbl(r) ( (double)((r)->num) / (double)((r)->den) )

double timecode_rate_to_double(TimecodeRate const * const r) {
  return TCtoDbl(r);
}

static void timecode_sample_to_time (TimecodeTime * const t, TimecodeRate const * const r, const double samplerate, const int64_t sample) {
  const double  fps_d = TCtoDbl(r);
  const int64_t fps_i = ceil(fps_d);

  if (r->drop) {
    int64_t frameNumber = floor((double)sample * fps_d / samplerate);

    /* there are 17982 frames in 10 min @ 29.97df */
    const int64_t D = frameNumber / 17982;
    const int64_t M = frameNumber % 17982;

    t->subframe =  rint(r->subframes * ((double)sample * fps_d / samplerate - (double)frameNumber));

    if (t->subframe == r->subframes && r->subframes != 0) {
            t->subframe = 0;
            frameNumber++;
    }

    frameNumber +=  18*D + 2*((M - 2) / 1798);

    t->frame  =    frameNumber % 30;
    t->second =   (frameNumber / 30) % 60;
    t->minute =  ((frameNumber / 30) / 60) % 60;
    t->hour   = (((frameNumber / 30) / 60) / 60);

  } else {
    double timecode_frames_left_exact;
    double timecode_frames_fraction;
    int64_t timecode_frames_left;
    const double frames_per_timecode_frame = samplerate / fps_d;
    const int64_t frames_per_hour = (int64_t)(3600 * fps_i * frames_per_timecode_frame);

    t->hour = sample / frames_per_hour;
    double sample_d = sample % frames_per_hour;

    timecode_frames_left_exact = sample_d / frames_per_timecode_frame;
    timecode_frames_fraction = timecode_frames_left_exact - floor(timecode_frames_left_exact);

    t->subframe = (int32_t) rint(timecode_frames_fraction * r->subframes);

    timecode_frames_left = (int64_t) floor (timecode_frames_left_exact);

    if (t->subframe == r->subframes && r->subframes != 0) {
      t->subframe = 0;
      timecode_frames_left++;
    }

    t->minute = timecode_frames_left / (fps_i * 60);
    timecode_frames_left = timecode_frames_left % (fps_i * 60);
    t->second = timecode_frames_left / fps_i;
    t->frame  = timecode_frames_left % fps_i;
  }
}

void timecode_framenumber_to_time (TimecodeTime * const t, TimecodeRate const * const r, const int64_t frameno) {
  timecode_sample_to_time(t, r, TCtoDbl(r), frameno);
}

void timecode_time_to_string (char *smptestring, TimecodeTime const * const t) {
  snprintf(smptestring, 12, "%02d:%02d:%02d:%02d",
      t->hour, t->minute, t->second, t->frame);
}

void timecode_framenumber_to_string (char *smptestring, TimecodeRate const * const r, const int64_t frameno) {
  TimecodeTime t;
  timecode_framenumber_to_time(&t, r, frameno);
  timecode_time_to_string(smptestring, &t);
}

// vim:sw=2 sts=2 ts=8 et:
