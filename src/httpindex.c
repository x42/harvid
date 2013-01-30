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
#define _GNU_SOURCE // asprintf
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

#ifndef MAX_PATH
# ifdef PATH_MAX
#  define MAX_PATH PATH_MAX
# else
#  define MAX_PATH (1024)
# endif
#endif

#define SL_SEP(string) (strlen(string)>0?(string[strlen(string)-1]=='/')?"":"/":"")

char *url_escape(const char *string, int inlength); // from httprotocol.c


static int print_html (int what, const char *burl, const char *path, const char *name, char *m, size_t n) {
  size_t off =0;
  switch(what) {
    case 1:
      {
      char *u1,*u2;
      u1=url_escape(path,0);
      u2=url_escape(name,0);
      off+=snprintf(m+off, n-off,
       "[<b>F</b>] <a href=\"%s?frame=100&amp;file=%s%s\">%s</a>",
        burl,u1,u2,
        name
        );
      //off+=snprintf(m+off, n-off,"&nbsp; <a href=\"http://localhost/sodan/seek.php?file=%s%s\" target=\"_blank\">Seek</a>",u1,u2);
      off+=snprintf(m+off, n-off,"<br/>\n");
      if (u1) free(u1);
      if (u2) free(u2);
      }
      break;
    case 0:
      {
      off+=snprintf(m+off, n-off,
       "[D]<a href=\"%s%s/\">%s</a><br/>\n",
        burl,
        name,name
        );
      }
      break;
    default:
      break;
  }
  return(off);
}


static int parse_direntry (const char *root, const char *burl, const char *path, const char *name, int opt, char *m, size_t n, int (*print_fn)(const int what, const char*, const char*, const char*, char*, size_t) ) {
  int rv= 0;
  //int len=strlen(name);
  //if (len < 5) return 0; // ".+\.[a-z{3}" = 5
  // TODO check files permission, pre-sort by extension?
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
  sprintf(dn,"%s%s%s",root,SL_SEP(root),path);

  dlog(LOG_DEBUG, "IndexDir: indexing '%s'\n",dn);
  if (!(D = opendir (dn)))  {
    dlog(LOG_WARNING, "IndexDir: could not open dir '%s'\n",dn);
    return 0;
  }
  while ((dd = readdir (D))) {
    if (dd->d_name[0]=='.') continue; // make optional
#if 0
    int delen=strlen(d->d_name);
    if (dd->d_name[0]=='.' && delen==1) continue; // '.'
    if (dd->d_name[0]=='.' && dd->d_name[1]=='.' && delen==2) continue; // '..'
#endif
    struct stat fs;
    char rn[MAX_PATH]; // absolute, starting at local root /
    sprintf(rn,"%s/%s",dn,dd->d_name);
    if(stat(rn,&fs)==0) {
      char fn[MAX_PATH]; // relative to this *root.
      sprintf(fn,"%s%s%s",path,SL_SEP(path),dd->d_name);
      if (S_ISDIR(fs.st_mode)) {
        if ((opt&1)==1) {
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
        off+=parse_direntry(root,burl,path,dd->d_name, opt, m+off, n-off, print_fn);
      }
    }
  }
  closedir(D);
  return(off);
}

#define IDXSIZ (65536*4) // TODO dynamic size

char *index_dir (const char *root, char *base_url, char *path, int opt) {
  char *sm = malloc(IDXSIZ * sizeof(char));
  int off =0;
  off+=snprintf(sm+off, IDXSIZ-off, DOCTYPE HTMLOPEN);
  off+=snprintf(sm+off, IDXSIZ-off, "<title>ICS Index</title></head>\n<body>\n<h2>ICS - Index</h2>\n<p>\n");
  int bl=strlen(base_url)-2;
  if (bl>1) {
    while (bl>0 && base_url[--bl]!='/');
    if (bl>0) {  // TODO: check for '/index/'
      base_url[bl]=0;
      off+=snprintf(sm+off, IDXSIZ-off, "<a href=\"%s/\">..</a><br/>\n",base_url);
      base_url[bl]='/';
    }
  }
  off+=parse_dir(root, base_url, path, opt, sm+off, IDXSIZ-off, print_html);
  //off+=snprintf(sm+off, STASIZ-off, "<hr/><p>sodankyla-ics/%s at %s:%i</p>", ICSVERSION, c->d->local_addr, c->d->local_port);
  off+=snprintf(sm+off, IDXSIZ-off, "\n</p>\n</body>\n</html>");
  return (sm);
}

// vim:sw=2 sts=2 ts=8 et:
