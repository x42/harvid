/*
   This file is part of harvid

   Copyright (C) 2008-2014 Robin Gareus <robin@gareus.org>

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
#include <sys/types.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include <dlog.h>
#include "httprotocol.h"
#include "htmlconst.h"
#include "enums.h"

#ifndef MAX_PATH
# ifdef PATH_MAX
#  define MAX_PATH PATH_MAX
# else
#  define MAX_PATH (1024)
# endif
#endif

extern int cfg_usermask;

char *url_escape(const char *string, int inlength); // from httprotocol.c

char *str_escape(const char *string, int inlength, const char esc) {
  char *ns;
  size_t i, o, a;
  const char *t = string;
  if (!string) return strdup("");
  if (inlength == 0) inlength = strlen(string)+1;
  a = inlength;
  while(*t && (t = strchr(t, '"'))) {
    a++; t++;
  }
  ns = malloc(a);
  for (i = 0, o = 0; i < a; ++i) {
    if (string[i] == '"') {
      ns[o++] = esc;
      ns[o++] = '"';
    } else {
      ns[o++] = string[i];
    }
  }
  return ns;
}

static void print_html (int what, const char *burl, const char *path, const char *name, time_t mtime,
    char **m, size_t *o, size_t *s, int *num) {
  switch(what) {
    case 1:
      {
      char *u1, *u2;
      u1 = url_escape(path, 0);
      u2 = url_escape(name, 0);

      if (cfg_usermask & USR_WEBSEEK) {
        rprintf("<li>[<b>F</b>] <a href=\"%sseek?frame=0&amp;file=%s%s%s\">%s</a>", burl, u1, SL_SEP(path), u2, name);
      } else {
        rprintf("<li>[<b>F</b>] <a href=\"%s?frame=0&amp;file=%s%s%s\">%s</a>", burl, u1, SL_SEP(path), u2, name);
      }

      rprintf(
       " [<a href=\"%sinfo?file=%s%s&amp;format=html\">info</a>]",
        burl, u1, u2
        );
      rprintf("</li>\n");
      if (u1) free(u1);
      if (u2) free(u2);
      (*num)++;
      }
      break;
    case 0:
      {
      char *u2 = url_escape(name, 0);
      rprintf(
       "<li>[D]<a href=\"%s%s%s/\">%s</a></li>\n",
        burl, SL_SEP(path), u2, name);
      free(u2);
      (*num)++;
      }
      break;
    default:
      break;
  }
}

static void print_csv (int what, const char *burl, const char *path, const char *name, time_t mtime,
    char **m, size_t *o, size_t *s, int *num) {
  switch(what) {
    case 1:
      {
      char *u1, *u2, *c1;
      u1 = url_escape(path, 0);
      u2 = url_escape(name, 0);
      c1 = str_escape(name, 0, '"');
      rprintf("F,\"%s\",\"%s%s%s\",\"%s\",%"PRIlld"\n", burl, u1, SL_SEP(path), u2, c1, (long long) mtime);
      free(u1); free(u2); free(c1);
      (*num)++;
      }
      break;
    case 0:
      {
      char *u2, *c1;
      u2 = url_escape(name, 0);
      c1 = str_escape(name, 0, '"');
      rprintf("D,\"%s%s%s/\",\"%s\",%"PRIlld"\n", burl, SL_SEP(path), u2, c1, (long long) mtime);
      free(u2); free(c1);
      (*num)++;
      }
      break;
    default:
      break;
  }
}

static void print_plain (int what, const char *burl, const char *path, const char *name, time_t mtime,
    char **m, size_t *o, size_t *s, int *num) {
  switch(what) {
    case 1:
      {
      char *u1, *u2;
      u1 = url_escape(path, 0);
      u2 = url_escape(name, 0);
      rprintf("f %s?file=%s%s%s\n", burl, u1, SL_SEP(path), u2);
      free(u1); free(u2);
      (*num)++;
      }
      break;
    case 0:
      {
      char *u2;
      u2 = url_escape(name, 0);
      rprintf("d %s%s%s/\n", burl, SL_SEP(path), u2);
      free(u2);
      (*num)++;
      }
      break;
    default:
      break;
  }
}


static void print_json (int what, const char *burl, const char *path, const char *name, time_t mtime,
    char **m, size_t *o, size_t *s, int *num) {
  switch(what) {
    case 1:
      {
      char *u1, *u2, *c1;
      u1 = url_escape(path, 0);
      u2 = url_escape(name, 0);
      c1 = str_escape(name, 0, '\\');
      rprintf("%s{\"type\":\"file\", \"baseurl\":\"%s\", \"file\":\"%s%s%s\", \"name\":\"%s\", \"mtime\":%"PRIlld"}",
          (*num > 0) ? ", ":"", burl, u1, SL_SEP(path), u2, c1, (long long) mtime);
      free(u1); free(u2); free(c1);
      (*num)++;
      }
      break;
    case 0:
      {
      char *u2, *c1;
      u2 = url_escape(name, 0);
      c1 = str_escape(name, 0, '\\');
      rprintf("%s{\"type\":\"dir\", \"indexurl\":\"%s%s%s/\", \"name\":\"%s\", \"mtime\":%"PRIlld"}",
          (*num > 0) ? ", ":"", burl, SL_SEP(path), u2, c1, (long long) mtime);
      free(u2); free(c1);
      (*num)++;
      }
      break;
    default:
      break;
  }
}


static void parse_direntry (const char *root, const char *burl, const char *path, const char *name, 
    time_t mtime, int opt,
    char **m, size_t *o, size_t *s, int *num,
    void (*print_fn)(const int what, const char*, const char*, const char*, time_t, char**, size_t*, size_t*, int *) ) {
  const int l3 = strlen(name) - 3;
  const int l4 = l3 - 1;
  const int l5 = l4 - 1;
  const int l6 = l5 - 1;
  const int l9 = l6 - 3;
  if ((l4 > 0 && ( !strcasecmp(&name[l4], ".avi")
                || !strcasecmp(&name[l4], ".mov")
                || !strcasecmp(&name[l4], ".ogg")
                || !strcasecmp(&name[l4], ".ogv")
                || !strcasecmp(&name[l4], ".mpg")
                || !strcasecmp(&name[l4], ".mov")
                || !strcasecmp(&name[l4], ".mp4")
                || !strcasecmp(&name[l4], ".mkv")
                || !strcasecmp(&name[l4], ".vob")
                || !strcasecmp(&name[l4], ".asf")
                || !strcasecmp(&name[l4], ".avs")
                || !strcasecmp(&name[l4], ".dts")
                || !strcasecmp(&name[l4], ".flv")
                || !strcasecmp(&name[l4], ".m4v")
        )) ||
      (l5 > 0 && ( !strcasecmp(&name[l5], ".h264")
                || !strcasecmp(&name[l5], ".webm")
        )) ||
      (l6 > 0 && ( !strcasecmp(&name[l6], ".dirac")
        )) ||
      (l9 > 0 && ( !strcasecmp(&name[l9], ".matroska")
        )) ||
      (l3 > 0 && ( !strcasecmp(&name[l3], ".dv")
                || !strcasecmp(&name[l3], ".ts")
        ))
     ) {
    char *url = strdup(burl);
    char *vurl = strstr(url, "/index"); // TODO - do once per dir.
    if (vurl) *++vurl = 0;
    print_fn(1, url, path, name, mtime, m, o, s, num);
    free(url);
  }
}

static int parse_dir (const int fd, const char *root, const char *burl, const char *path, int opt,
    char **m, size_t *o, size_t *s, int *num,
    void (*print_fn)(const int what, const char*, const char*, const char*, time_t, char**, size_t*, size_t*, int *) ) {
  DIR  *D;
  struct dirent *dd;
  char dn[MAX_PATH];
  int rv = 0;
  snprintf(dn, MAX_PATH, "%s%s%s", root, SL_SEP(root), path);

  debugmsg(DEBUG_ICS, "IndexDir: indexing '%s'\n", dn);
  if (!(D = opendir (dn)))  {
    dlog(LOG_WARNING, "IndexDir: could not open dir '%s'\n", dn);
    return -1;
  }

  while ((dd = readdir (D))) {
    struct stat fs;
    char rn[MAX_PATH]; // absolute, starting at local root /
    if (dd->d_name[0] == '.') continue; // make optional
#if 0
    int delen = strlen(d->d_name);
    if (delen == 1 && dd->d_name[0] == '.') continue; // '.'
    if (delen == 2 && dd->d_name[0] == '.' && dd->d_name[1] == '.') continue; // '..'
#endif

    snprintf(rn, MAX_PATH, "%s/%s", dn, dd->d_name);
    if(stat(rn, &fs) == 0) { // XXX lstat vs stat
      char fn[MAX_PATH]; // relative to this *root.
      snprintf(fn, MAX_PATH, "%s%s%s", path, SL_SEP(path), dd->d_name);
      if (S_ISDIR(fs.st_mode)) {
        if ((opt&OPT_FLAT) == OPT_FLAT) {
          char pn[MAX_PATH];
          snprintf(pn, MAX_PATH, "%s%s%s/", path, SL_SEP(path), dd->d_name);
          if ((rv = parse_dir(fd, root, burl, pn, opt, m, o, s, num, print_fn))) {
            if (rv == -1) rv = 0; // opendir failed -- continue
            else break;
          }
          if (strlen(*m) > 0) {
            int tx = CSEND(fd, (*m));
            if (tx > 0) {
              (*o) = 0;
              (*m)[0] = '\0';
            } else if (tx < 0) {
              debugmsg(DEBUG_ICS, "abort indexing\n");
              rv = -2;
              break;
            }
          }
        } else {
          print_fn(0, burl, path, dd->d_name, fs.st_mtime, m, o, s, num);
        }
      }
      else if (
#ifndef HAVE_WINDOWS
          S_ISLNK(fs.st_mode) ||
#endif
          S_ISREG(fs.st_mode)) {
        parse_direntry(root, burl, path, dd->d_name, fs.st_mtime, opt, m, o, s, num, print_fn);
      }
    }
  }
  closedir(D);
  return rv;
}

void hdl_index_dir (int fd, const char *root, char *base_url, char *path, int fmt, int opt) {
  size_t off = 0;
  size_t ss = 1024;
  char *sm = malloc(ss * sizeof(char));
  const int bo = 5 + strstr(base_url, "/index") - base_url;
  int bl = strlen(base_url) - 1;
  int num = 0;
  sm[0] = '\0';

  switch (fmt) {
    case OUT_PLAIN:
      break;
    case OUT_JSON:
      {
      char *p1 = str_escape(path, 0, '\\');
      raprintf(sm, off, ss, "{\"path\":\"%s\", \"index\":[", p1);
      free(p1);
      }
      break;
    case OUT_CSV:
      break;
    default:
      raprintf(sm, off, ss, DOCTYPE HTMLOPEN);
      raprintf(sm, off, ss, "<title>harvid Index</title>\n");
      raprintf(sm, off, ss, "<style type=\"text/css\">\nli {float:left; margin:0 .5em .5em 0em; list-style-type:none;}\n</style>\n");
      raprintf(sm, off, ss, "</head>\n");
      raprintf(sm, off, ss, HTMLBODY);
      raprintf(sm, off, ss, "<h2>harvid - Index</h2>\n");
      raprintf(sm, off, ss, "<p>Path: %s</p>\n<ul>\n", strlen(path) > 0 ? path : "<em>(docroot)</em>");
      break;
  }

  if (bl > bo) {
    while (bl > bo && base_url[--bl] != '/') ;
    if (bl > bo) {
      base_url[bl] = 0;
      switch (fmt) {
        case OUT_PLAIN:
        case OUT_JSON:
        case OUT_CSV:
        break;
        default:
        raprintf(sm, off, ss, "<li>[D]<a href=\"%s/\">..</li>\n", base_url);
        break;
      }
      base_url[bl] = '/';
    }
  }

  switch (fmt) {
    case OUT_PLAIN:
      parse_dir(fd, root, base_url, path, opt, &sm, &off, &ss, &num, print_plain);
      raprintf(sm, off, ss, "# total: %d\n", num);
      break;
    case OUT_JSON:
      parse_dir(fd, root, base_url, path, opt, &sm, &off, &ss, &num, print_json);
      raprintf(sm, off, ss, "]}");
      break;
    case OUT_CSV:
      parse_dir(fd, root, base_url, path, opt, &sm, &off, &ss, &num, print_csv);
      break;
    default:
      parse_dir(fd, root, base_url, path, opt, &sm, &off, &ss, &num, print_html);
      raprintf(sm, off, ss, "</ul><div style=\"clear:both;\"></div>\n<p>Total Entries: %d</p>\n", num);
      raprintf(sm, off, ss, "<hr/><div style=\"text-align:center; color:#888;\">"SERVERVERSION"</div>");
      raprintf(sm, off, ss, "</body>\n</html>");
      break;
  }

  if (strlen(sm) > 0) CSEND(fd, sm);
  free(sm);
}

// vim:sw=2 sts=2 ts=8 et:
