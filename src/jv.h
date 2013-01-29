/**
 * @file jv.h
 * @author Robin Gareus <robin@gareus.org>
 * @brief video object abstraction
 */
#ifndef _JV_H
#define _JV_H

#include <stdlib.h>
#include <stdint.h>     /* uint8_t */
#include "timecode.h"

/**
 * representation for video frame numbers
 */
typedef int64_t jv_framenumber; // TODO - search&replace -> jv.h

/**
 * @brief video-file information
 */
typedef struct {
  int movie_width;        ///< read-only image-size
  int movie_height;       ///< read-only image-size
  float movie_aspect;     ///< read-only original aspect-ratio
  int out_width;          ///< actual output image geometry
  int out_height;         ///< may be overwritten
  TimecodeRate framerate; ///< framerate num/den&flags
  jv_framenumber frames;  ///< duration of file in frames
  size_t buffersize;      ///< size in bytes used for an image of out_width x out_height at render_rmt (JVINFO)
} JVINFO;

/**
 * @brief video decoder settings
 *
 * arguments for opening a video file (set defaults) or when
 * decoding (override defaults) a video frame.
 */
typedef struct {
  char *file_name;///< path to media file
  int render_fmt; ///< prepare buffer(s) for this format
  int out_width;  ///< output image width
  int out_height; ///< output image height
} JVARGS;
/**
 * @brief video object fixture
 *
 * represents the current session state (fixture) for a given video object.
 * vs_getobj() populates the session_args which can be passed on
 * to jv_get_id().
 *
 * TODO: rename this struct once the session API is in place.
 */
typedef struct {
  char *file_name; ///< resolved file name for the given object
  jv_framenumber frame;///< video frame number.
} jvsession_args;

#if 0
typedef struct {
  void *handle; // used by main program
  void (*init)(void);
  void (*cleanup)(void);
  void (*create)(void**, void *, void *);
  void (*destroy)(void**);
  int  (*open_movie)(void*, JVARGS*);
  int  (*close_movie)(void*);
  void (*render)(void *, unsigned long, uint8_t* , int , int , int , int , int);
  void (*get_info) (void*, JVINFO *);
  void (*get_framerate) (void*, TimecodeRate *); // deprecated -> get_info
  // non gjvt:
  //void (*set_framerate) (void*, TimecodeRate *fr);
  //void (*do_try_this_file_and_exit) (void*, char*);
  //void (*get_buffersize) (void*, size_t*);
  //void (*init_moviebuffer) (void*);
  const char *name;
  const char *version;
} JVPLUGIN;
#endif

/** reset a JVARGS struct
 * @param a JVARGS struct to clear
 */
void init_jva (JVARGS*a);
/** initialise a JVINFO struct
 * @param i JVINFO struct to initialize
 */
void init_jvi (JVINFO*i);
/** clear and free JVINFO struct
 * @param i JVINFO struct to free
 */
void free_jvi (JVINFO*i);

/** clear and free jvsession_args struct
 * @param a jvsession_args struct to free
 */
void free_jvs (jvsession_args *a);

/* jv-object API  - will take *p as 1st argument */

/** create and allocate a decoder control object
 * @param p pointer to allocated object
 */
void decoder_control_create(void **p);
/** close and destroy a decoder control object
 * @param p object pointer to free
 */
void decoder_control_destroy(void **p);
/**
 * request a video-object id for the given file
 *
 * @param p pointer to a decoder-control object
 * @param fn file name to look up
 * @return file-id use with: jv_get_info() or jv_get_decoder()
 */
int jv_get_id(void *p, const char *fn);
/**
 * write debug info to stdout
 * @param p pointer to a decoder-control object
 */
void dumpdecoderctl(void *p); // debug to stdout
/**
 * HTML format debug info and store at most \a n bytes of the message to \a m
 * @param p pointer to a decoder-control object
 * @param m pointer to where result message is stored
 * @param n max length of message.
 */
size_t formatdecoderctlinfo(void *p, char *m, size_t n);
/**
 * request JVINFO video-info for given decoder-object
 * @param p  pointer to a decoder-control object
 * @param id id of the decoder
 * @param i returned data
 * @return 0 on success, -1 otherwise
 */
int jv_get_info(void *p, int id, JVINFO *i);
/**
 * set new scaling factors and return updated JVINFO
 * @param p  pointer to a decoder-control object
 * @param id id of the decoder
 * @param w width or -1 to use aspect-ratio and height
 * @param h height or -1 to use aspect-ratio and width (if both w and h are -1 - no scaling is performed)
 * @param i optional - if not NULL \ref jv_get_info is called to fill in the data
 * @return 0 on success, -1 otherwise
 */
int jv_get_info_scale(void *p, int id, JVINFO *i, int w, int h);

// the following are wrapped by the frame-cache: use xjv_get_buffer().
int jv_get_decoder(void *p, int uuid, int id); // returns decoder-uuid reference and locks the decoder.
int jv_decode(void *p, int uuid, unsigned long frame, uint8_t *b, int w, int h);
void jv_release_decoder(void *p, int uuid);

#endif
