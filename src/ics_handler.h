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
