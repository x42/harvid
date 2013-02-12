/*
   This file is part of harvid

   Copyright (C) 2008-2013 Robin Gareus <robin@gareus.org>

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

#include "daemon_log.h"
#include "httprotocol.h"
#include "enums.h"

#ifndef MAX_PATH
# ifdef PATH_MAX
#  define MAX_PATH PATH_MAX
# else
#  define MAX_PATH (1024)
# endif
#endif

#define SL_SEP(string) (strlen(string)>0?(string[strlen(string)-1]=='/')?"":"/":"")

char *url_escape(const char *string, int inlength); // from httprotocol.c

char *csv_escape(const char *string, int inlength, const char esc) {
  char *ns;
  size_t i,o,a;
  const char *t = string;
  if (!string) return strdup("");
  if (inlength == 0) inlength = strlen(string)+1;
  a = inlength;
  while(*t && (t=strchr(t, '"'))) {
    a++; t++;
  }
  ns = malloc(a);
  for (i=0,o=0; i<a; ++i) {
    if (string[i] == '"') {
      ns[o++] = esc;
      ns[o++] = '"';
    } else {
      ns[o++] = string[i];
    }
  }
  return ns;
}

static int print_html (int what, const char *burl, const char *path, const char *name, char *m, size_t n) {
  size_t off =0;
  switch(what) {
    case 1:
      {
      char *u1,*u2;
      u1=url_escape(path,0);
      u2=url_escape(name,0);
      off+=snprintf(m+off, n-off,
       "[<b>F</b>] <a href=\"%s?frame=100&amp;file=%s%s%s\">%s</a>",
        burl, u1, SL_SEP(path), u2, name);
      off+=snprintf(m+off, n-off,
       " [<a href=\"%sinfo?file=%s%s&amp;format=html\">info</a>]",
        burl,u1,u2
        );
      off+=snprintf(m+off, n-off,"<br/>\n");
      if (u1) free(u1);
      if (u2) free(u2);
      }
      break;
    case 0:
      {
      char *u2 = url_escape(name,0);
      off+=snprintf(m+off, n-off,
       "[D]<a href=\"%s%s%s/\">%s</a><br/>\n",
        burl, SL_SEP(path), u2, name);
      free(u2);
      }
      break;
    default:
      break;
  }
  return(off);
}

static int print_csv (int what, const char *burl, const char *path, const char *name, char *m, size_t n) {
  size_t off =0;
  switch(what) {
    case 1:
      {
      char *u1, *u2, *c1;
      u1=url_escape(path, 0);
      u2=url_escape(name, 0);
      c1=csv_escape(name, 0, '"');
      off+=snprintf(m+off, n-off,
       "F,\"%s\",\"%s%s%s\",\"%s\"\n", burl, u1, SL_SEP(path), u2, c1);
      free(u1); free(u2); free(c1);
      }
      break;
    case 0:
      {
      char *u2, *c1;
      u2=url_escape(name, 0);
      c1=csv_escape(name, 0, '"');
      off+=snprintf(m+off, n-off,
       "D,\"%s%s%s\",\"%s\"\n", burl, SL_SEP(path), u2, c1);
      free(u2); free(c1);
      }
      break;
    default:
      break;
  }
  return(off);
}


static int parse_direntry (const char *root, const char *burl, const char *path, const char *name, int opt, char *m, size_t n, int (*print_fn)(const int what, const char*, const char*, const char*, char*, size_t) ) {
  int rv= 0;
  char *url=strdup(burl);
  char *vurl=strstr(url,"/index"); // TODO - do once per dir.
  if (vurl) *++vurl = 0;
  int l=strlen(name)-4;
  if ( l>0 && ( !strcmp(&name[l], ".avi")
              ||!strcmp(&name[l], ".mov")
              ||!strcmp(&name[l], ".ogg")
              ||!strcmp(&name[l], ".ogv")
              ||!strcmp(&name[l], ".mpg")
              ||!strcmp(&name[l], ".mov")
              ||!strcmp(&name[l], ".mp4")
              ||!strcmp(&name[l], ".mkv")
              ||!strcmp(&name[l], ".vob")
              ||!strcmp(&name[l], ".asf")
              ||!strcmp(&name[l], ".avs")
              ||!strcmp(&name[l], ".dts")
              ||!strcmp(&name[l], ".flv")
              ||!strcmp(&name[l], ".m4v")
              ||!strcmp(&name[l], ".matroska")
              ||!strcmp(&name[l], ".h264")
              ||!strcmp(&name[l], ".dv")
              ||!strcmp(&name[l], ".dirac")
              ||!strcmp(&name[l], ".webm")
              )
     ) {
    rv= print_fn(1, url, path, name, m, n);
  }
  free(url);
  return rv;
}

int parse_dir (const char *root, const char *burl, const char *path, int opt, char *m, size_t n, int (*print_fn)(const int what, const char*, const char*, const char*, char*, size_t) ) {
  DIR  *D;
  struct dirent *dd;
  char dn[MAX_PATH];
  size_t off =0;
  sprintf(dn,"%s%s%s", root, SL_SEP(root), path);

  dlog(LOG_DEBUG, "IndexDir: indexing '%s'\n", dn);
  if (!(D = opendir (dn)))  {
    dlog(LOG_WARNING, "IndexDir: could not open dir '%s'\n", dn);
    return 0;
  }

  while ((dd = readdir (D))) {
    struct stat fs;
    char rn[MAX_PATH]; // absolute, starting at local root /
    if (dd->d_name[0]=='.') continue; // make optional
#if 0
    int delen=strlen(d->d_name);
    if (delen==1 && dd->d_name[0]=='.' ) continue; // '.'
    if (delen==2 && dd->d_name[0]=='.' && dd->d_name[1]=='.') continue; // '..'
#endif

    sprintf(rn,"%s/%s", dn, dd->d_name);
    if(stat(rn,&fs)==0) {
      char fn[MAX_PATH]; // relative to this *root.
      sprintf(fn,"%s%s%s", path, SL_SEP(path), dd->d_name);
      if (S_ISDIR(fs.st_mode)) {
        if ((opt&OPT_FLAT) == OPT_FLAT) {
          char pn[MAX_PATH];
          sprintf(pn,"%s%s%s/",path, SL_SEP(path), dd->d_name);
          off+=parse_dir(root, burl, pn, opt, m+off, n-off, print_fn);
        } else {
          off+=print_fn(0, burl, path, dd->d_name, m+off, n-off);
        }
      }
      else if (
#ifndef HAVE_WINDOWS
          S_ISLNK(fs.st_mode) ||
#endif
          S_ISREG(fs.st_mode)) {
        off+=parse_direntry(root, burl, path, dd->d_name, opt, m+off, n-off, print_fn);
      }
    }
  }
  closedir(D);
  return(off);
}

#define IDXSIZ (65536*4) // TODO dynamic size

char *hdl_index_dir (const char *root, char *base_url, char *path, int opt) {
  char *sm = malloc(IDXSIZ * sizeof(char));
  int off = 0;
  int bl = strlen(base_url) - 2;

  if ((opt&OPT_CSV) == 0) {
    off+=snprintf(sm+off, IDXSIZ-off, DOCTYPE HTMLOPEN);
    off+=snprintf(sm+off, IDXSIZ-off, "<title>harvid Index</title></head>\n<body>\n<h2>harvid - Index</h2>\n<p>\n");
  }

  if (bl>1) {
    while (bl > 0 && base_url[--bl] != '/');
    if (bl>0) {  // TODO: check for '/index/'
      base_url[bl]=0;
      if ((opt&OPT_CSV) == 0) {
        off+=snprintf(sm+off, IDXSIZ-off, "<a href=\"%s/\">..</a><br/>\n", base_url);
      }
      base_url[bl]='/';
    }
  }

  if ((opt&OPT_CSV) == 0) {
    off+=parse_dir(root, base_url, path, opt, sm+off, IDXSIZ-off, print_html);
    off+=snprintf(sm+off, IDXSIZ-off, "<hr/><p>"SERVERVERSION"</p>");
    off+=snprintf(sm+off, IDXSIZ-off, "\n</p>\n</body>\n</html>");
  } else {
    off+=parse_dir(root, base_url, path, opt, sm+off, IDXSIZ-off, print_csv);
  }

  sm[IDXSIZ-1] = '\0';
  return (sm);
}

// vim:sw=2 sts=2 ts=8 et:
