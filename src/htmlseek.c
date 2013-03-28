#include <stdio.h>

#include <harvid.h>
//#include <dlog.h>
#include "socket_server.h"
#include "httprotocol.h"
#include "ics_handler.h"
#include "htmlconst.h"

extern void *dc; // decoder control
extern void *vc; // video cache

#define FIHSIZ 4096

char *hdl_file_seek (CONN *c, ics_request_args *a) {
  VInfo ji;
  unsigned short vid;
	int err = 0;
  vid = dctrl_get_id(vc, dc, a->file_name);
  jvi_init(&ji);
  if ((err=dctrl_get_info(dc, vid, &ji))) {
    if (err == 503) {
      httperror(c->fd, 503, "Service Temporarily Unavailable", "<p>No decoder is available. The server is currently busy or overloaded.</p>");
    } else {
      httperror(c->fd, 500, "Service Unavailable", "<p>No decoder is available: File is invalid (no video track, unknown codec, invalid geometry,..)</p>");
    }
    return NULL;
  }
  char *im = malloc(FIHSIZ * sizeof(char));
  int off = 0;
  char smpte[14];
  timecode_framenumber_to_string(smpte, &ji.framerate, ji.frames);

  off+=snprintf(im+off, FIHSIZ-off, DOCTYPE HTMLOPEN);
  off+=snprintf(im+off, FIHSIZ-off, "<title>harvid file info</title>\n");
  off+=snprintf(im+off, FIHSIZ-off,
"<script type=\"text/javascript\" src=\"/seek.js\"></script>\n" \
"<script type=\"text/javascript\"><!--\n" \
"  var fps=%.2f;\n" \
"  var lastframe=%"PRId64";\n" \
"  var fileid='%s';\n" \
"--></script>",
    timecode_rate_to_double(&ji.framerate),
    ji.frames - 1, a->file_qurl);

  off+=snprintf(im+off, FIHSIZ-off, "</head>\n");
  off+=snprintf(im+off, FIHSIZ-off, HTMLBODY);
  off+=snprintf(im+off, FIHSIZ-off, "<div style=\"text-align:center; background-color:#333; color:#fff; margin:1em; padding: 1em; border:1px solid #ccc; user-select:none; -moz-user-select: -moz-none; -webkit-user-select: none;\">\n");
  off+=snprintf(im+off, FIHSIZ-off, "<p style=\"margin-bottom:2em;\">");
  off+=snprintf(im+off, FIHSIZ-off, "<em>File</em>: %s<br/>\n", a->file_qurl);
  off+=snprintf(im+off, FIHSIZ-off, "<em>Geometry</em>: %ix%i, \n", ji.movie_width, ji.movie_height);
  off+=snprintf(im+off, FIHSIZ-off, "<em>Aspect-Ratio</em>: %.3f, \n", ji.movie_aspect);
  off+=snprintf(im+off, FIHSIZ-off, "<em>Framerate</em>: %.2f, \n", timecode_rate_to_double(&ji.framerate));
  off+=snprintf(im+off, FIHSIZ-off, "<em>Duration</em>: %s\n", smpte);
  off+=snprintf(im+off, FIHSIZ-off, "</p>\n");
  off+=snprintf(im+off, FIHSIZ-off,
"<div style=\"height:320px;\"><img src=\"\" alt=\"\" id=\"sframe\" style=\"border:4px solid black\"/></div>\n" \
"<div>\n" \
" <div style=\"width:560px; margin:2em auto .5em auto; padding:0.5em; border:3px double black;\">\n" \
"  <div style=\"position:relative; width:501px; height:1em; background-color:#cccccc; cursor:crosshair; margin:.25em auto; float:left;\" id=\"slider\">\n" \
"    <div style=\"position:absolute; top:0px; left:0px; width:25px; z-index:95; height:1em; background-color:#666; cursor:text;\" id=\"knob\"></div>\n" \
"  </div>\n" \
"\n" \
"  <div style=\"float:right; max-width:50px; overflow:hidden;\">\n" \
"   <div id=\"stepper_setup\" style=\"display:block;\">\n" \
"    <button type=\"button\" onclick=\"smode(1);\">click</button>\n" \
"   </div>\n" \
"   <div id=\"stepper_active\" style=\"display:none;\">\n" \
"    <button type=\"button\" onclick=\"smode(0);\">hover</button>\n" \
"   </div>\n" \
"  </div>\n" \
"  <div style=\"clear:both;\"></div>\n" \
" </div>\n" \
"\n" \
"  <table style=\"font-family:monospace; font-size:400%%; margin:0 auto;\">\n" \
"  <tr>\n" \
"    <td id=\"hour\">00</td><td>:</td>\n" \
"    <td id=\"min\">00</td><td>:</td>\n" \
"    <td id=\"sec\">00</td><td>.</td>\n" \
"    <td id=\"frame\">00</td>\n" \
"  </tr>\n" \
"  </table>\n" \
"\n" \
"</div>\n" \
"\n");

  off+=snprintf(im+off, FIHSIZ-off,
"<script type=\"text/javascript\"><!--\n" \
"  setslider(%"PRId64");\n" \
"  settc(%"PRId64");\n" \
"  seek('%s',%"PRId64");\n" \
"  document.getElementById('slider').onmousedown=movestep;\n" \
"--></script>\n",
    ji.frames/3, ji.frames/3, a->file_qurl, ji.frames/3);

  off+=snprintf(im+off, FIHSIZ-off, "</div>\n");
  off+=snprintf(im+off, FIHSIZ-off, HTMLFOOTER, c->d->local_addr, c->d->local_port);
  off+=snprintf(im+off, FIHSIZ-off, "</body>\n</html>");
  jvi_free(&ji);
  return (im);
}

// vim:sw=2 sts=2 ts=8 et:
