#include <stdio.h>
#include "socket_server.h"
#include "decoder_ctrl.h"

#include "httprotocol.h"
#include "ics_handler.h"
#include "htmlconst.h"

extern void *dc; // decoder control
extern void *vc; // video cache

#define FIHSIZ 4096

char *hdl_file_seek (CONN *c, ics_request_args *a) {
  VInfo ji;
  unsigned short vid;
  vid = dctrl_get_id(vc, dc, a->file_name);
  jvi_init(&ji);
  if (dctrl_get_info(dc, vid, &ji)) {
    return NULL;
  }
  char *im = malloc(FIHSIZ * sizeof(char));
  int off = 0;
  char smpte[14], *tmp;
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
    ji.frames, a->file_qurl);

  off+=snprintf(im+off, FIHSIZ-off, "</head>\n");
  off+=snprintf(im+off, FIHSIZ-off, HTMLBODY);
  off+=snprintf(im+off, FIHSIZ-off, CENTERDIV);
  off+=snprintf(im+off, FIHSIZ-off, "<h2>Video Inspector</h2>\n\n");
  tmp = url_escape(a->file_qurl, 0);
  off+=snprintf(im+off, FIHSIZ-off, "<p>File: <a href=\"/?frame=0&amp;file=%s\">%s</a></p><ul>\n",
      tmp, a->file_qurl); free(tmp);
  off+=snprintf(im+off, FIHSIZ-off, "<li>Geometry: %ix%i</li>\n", ji.movie_width, ji.movie_height);
  off+=snprintf(im+off, FIHSIZ-off, "<li>Aspect-Ratio: %.3f</li>\n", ji.movie_aspect);
  off+=snprintf(im+off, FIHSIZ-off, "<li>Framerate: %.2f</li>\n", timecode_rate_to_double(&ji.framerate));
  off+=snprintf(im+off, FIHSIZ-off, "<li>Duration: %s</li>\n", smpte);
  off+=snprintf(im+off, FIHSIZ-off, "</ul>\n</div>\n");
  off+=snprintf(im+off, FIHSIZ-off, "<div style=\"text-align:center; background-color:#333; color:#fff; margin:1em; padding: 1em; border:1px solid #ccc;\">\n");
  off+=snprintf(im+off, FIHSIZ-off,
"<div style=\"height:320px;\"><img src=\"\" alt=\"\" id=\"sframe\"/></div>\n" \
"<div>\n" \
"  <div style=\"position:relative; width:500px; height:14px; background-color:#cccccc; cursor:crosshair; margin:0 auto;\" id=\"slider\">\n" \
"    <div style=\"position:absolute; top:0px; left:0px; width:25px; z-index:95; height:14px; background-color:#666; cursor:text;\" id=\"knob\">&nbsp;</div>\n" \
"  </div>\n" \
"\n" \
"  <table style=\"font-family:monospace; font-size:400%%; margin:0 auto;\">\n" \
"  <tr>\n" \
"    <td id=\"hour\">00</td><td>:</td>\n" \
"    <td id=\"min\">00</td><td>:</td>\n" \
"    <td id=\"sec\">00</td><td>.</td>\n" \
"    <td id=\"frames\">00</td>\n" \
"  </tr>\n" \
"  </table>\n" \
"\n" \
"  <div>\n" \
"  <form onsubmit=\"return false;\" action=\"\">\n" \
"   <div id=\"stepper_setup\" style=\"display:block;\">\n" \
"    <button type=\"button\" onclick=\"smode(1);\">mouse-over stepper @</button>\n" \
"    <input maxlength=\"2\" size=\"2\" name=\"steps\" value=\"30\" id=\"numsteps\"/> steps\n" \
"   </div>\n" \
"   <div id=\"stepper_active\" style=\"display:none;\">\n" \
"    <button type=\"button\" onclick=\"smode(0);\">use slider</button>\n" \
"   </div>\n" \
"  </form>\n" \
"  </div>\n" \
"\n" \
"</div>\n" \
"\n");

  off+=snprintf(im+off, FIHSIZ-off,
"<script type=\"text/javascript\"><!--\n" \
"  setslider(%"PRId64");\n" \
"  settc(%"PRId64");\n" \
"  seek('%s',%"PRId64");\n" \
"  document.getElementById('slider').onmousedown=moveknob;\n" \
"  document.getElementById('slider').onmousemove=movestep;\n" \
"--></script>\n",
    ji.frames/3, ji.frames/3, a->file_qurl, ji.frames/3);

  off+=snprintf(im+off, FIHSIZ-off, "</div>\n");
  off+=snprintf(im+off, FIHSIZ-off, HTMLFOOTER, c->d->local_addr, c->d->local_port);
  off+=snprintf(im+off, FIHSIZ-off, "</body>\n</html>");
  jvi_free(&ji);
  return (im);
}
