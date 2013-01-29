#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "daemon_log.h"
#include "ffcompat.h"
#include "httprotocol.h"
#include "ics_handler.h"

/** Compare Transport Protocol request */
#define CTP(CMPPATH) \
	(  strncasecmp(protocol,  "HTTP/", 5) == 0 \
	&& strncasecmp(path, CMPPATH, strlen(CMPPATH)) == 0 \
	&& strcasecmp (method_str, "GET") == 0 )


/** session/ command macro */
#define SLISTCMD(COMMAND, RFORMAT, ...) \
	switch (output_format) { \
		case FORMAT_HTML: \
		case FORMAT_RAW: \
			if (ish_session_ ## COMMAND (c, sn, cb_ ## RFORMAT ## _txt, &reply , ##__VA_ARGS__)) \
				output_format=FORMAT_NULL; \
		break;\
		default:  \
			output_format=FORMAT_INVALID; \
	}

/** parse query params macros */
#define PHQ_int(KEY, RV) \
  if ((tmp=parse_http_query2(query, KEY))) { RV=atoi(tmp); free(tmp); }

#define PHQ_int64(KEY, RV) \
  if ((tmp=parse_http_query2(query, KEY))) { RV=(int64_t)atol(tmp); free(tmp); }

#define PHQ_text(KEY, RV) \
  if ((tmp=parse_http_query2(query, KEY))) { RV=tmp; }


#ifndef HAVE_WINDOWS
#define CSEND(FD,STATUS) write(FD, STATUS, strlen(STATUS))
#else
#define CSEND(FD,STATUS) send(FD, STATUS, strlen(STATUS),0)
#endif

#define SEND200(MSG) \
	send_http_status_fd(c->fd, 200); \
	send_http_header_fd(c->fd, 200, NULL); \
	CSEND(c->fd, MSG);


///////////////////////////////////////////////////////////////////
// Helper fn

/**
 * check for invalid or potentially malicious path.
 */
static int check_path(char *f) {
	int len = strlen(f);
	// TODO: f is an url_unescape()d value and may contain
	// malicious non-ASCII chars.

	/* check for possible 'escape docroot' trickery */
	if ( f[0] == '/' || strcmp( f, ".." ) == 0 || strncmp( f, "../", 3 ) == 0
			|| strstr( f, "/../" ) != (char*) 0
			|| strcmp( &(f[len-3]), "/.." ) == 0 ) return -1;
	return 0;
}

#if 0
/**
 * path to file
 * prefix docroot, check if file exists and if so return mtime of it.
 * note: modifies 'filename' char *
 */
static int check_file(CONN *c, char **fn) {
	if (!fn) return -3;
	char *file_name = malloc(1+strlen(c->d->docroot)+strlen(*fn)*sizeof(char));
	sprintf(file_name,"%s%s",c->d->docroot,*fn);
	free(*fn); *fn=file_name;
	// test if file exists or send 404
	struct stat sb;
	if (stat(file_name, &sb) == -1) {
		dlog(DLOG_WARNING, "CKF: file not found: '%s'\n", file_name);
		httperror(c->fd, 404, "Not Found", "file not found." );
		return(-1);
	}	
	// check file permissions.
	if (access(file_name, R_OK)) {
		dlog(DLOG_WARNING, "CKF: permission denied for file: '%s'\n", file_name);
		httperror(c->fd, 403, NULL, NULL);
		return(-2);
	}
	return sb.st_mtime;
}
#endif

///////////////////////////////////////////////////////////////////

// TODO: parse ouput format [png,jpg|jpeg,ppm] - see v_writer.h
// XXX: actually deprecate this mess..
static int parse_http_query(CONN *c, char *query, httpheader *h, ics_request_args *a) {
	int doit=0;
	char *fn = NULL; //file_name arg pointer
	char *sa = NULL; //save_as arg pointer

	a->decode_fmt = PIX_FMT_RGB24; // TODO - this is yet unused
	a->render_fmt = 2; //OUT_FMT_PNG;
	a->frame=0;
	a->out_width = a->out_height = -1; // auto-set
	a->str_duration=-1;
	a->str_audio=1;
	a->str_vcodec=a->str_acodec=a->str_container=0;

	void parse_param(char *kvp) {
		char *sep;
		if (!(sep=strchr(kvp,'='))) return;
		*sep='\0';
		char *val = sep+1;
		if (!val || strlen(val) < 1 || strlen(kvp) <1) return;

		//dlog(DLOG_DEBUG, "QUERY '%s'->'%s'\n",kvp,val);

		if (!strcmp (kvp, "frame")) {
			a->frame      = atoi(val);
			doit         |= 1;
		} else if (!strcmp (kvp, "w")) {
			a->out_width  = atoi(val);
		} else if (!strcmp (kvp, "h")) {
			a->out_height = atoi(val);
		} else if (!strcmp (kvp, "file")) {
			fn = url_unescape(val, 0, NULL);
			doit         |= 2;
		} else if (!strcmp (kvp, "save_as")) {
			sa = url_unescape(val, 0, NULL);
		} else if (!strcmp (kvp, "format")) {
						 if (!strcmp(val,"jpg") )  a->render_fmt=1;
				else if (!strcmp(val,"jpeg"))  a->render_fmt=1;
				else if (!strcmp(val,"ppm") )  a->render_fmt=3;
				else if (!strcmp(val,"raw") )  a->render_fmt=0;
				else if (!strcmp(val,"rgb") )  a->render_fmt=0;
				else if (!strcmp(val,"rgba")) {a->render_fmt=0; a->decode_fmt=PIX_FMT_RGBA;} // decode_fmt is not yet impl.
				else if (!strcmp(val,"json"))  a->render_fmt=1; // used with '/info' - TODO
				else if (!strcmp(val,"plain"))  a->render_fmt=2; // used with '/info' - TODO
		}
	}

	// parse query parameters
	char *t, *s = query;
	while(s && (t=strpbrk(s,"&?"))) {
		*t='\0';
		parse_param(s);
		s=t+1;
	}
	if (s) parse_param(s);

	// check for illegal paths
	if (!fn || check_path(fn)) {
		//httperror(c->fd, 400, "Bad Request", "Illegal filename." );
		httperror(c->fd, 404, "File not found.", "File not found." );
		return(0);
	}

	if (doit&3) {
		if (sa) {
			// TODO: strip slashes.. ASCIIfy..
			//a->save_as = sa;
			a->save_as = strdup(sa); free(sa);
		}
		char *t2 = fn;
		#if 0 // hardcoded no-folder protection
		char *t1;
		while ((t1=strpbrk(t2, " /"))) t2=t1+1;
		#endif
		if (t2) {
			a->file_name = malloc(1+strlen(c->d->docroot)+strlen(t2)*sizeof(char)); // TODO free this one - ACK - done below in hardcoded_video() at XXX
			sprintf(a->file_name,"%s%s",c->d->docroot,t2);
		}
		free(fn);

		// test if file exists or send 404
		struct stat sb;
		if (stat(a->file_name, &sb) == -1) {
			dlog(DLOG_WARNING, "CON: file not found: '%s'\n", a->file_name);
			httperror(c->fd, 404, "Not Found", "file not found." );
			return(0);
		}

		if (h) h->mtime = sb.st_mtime; // XXX - check  - only used with 'hardcoded_video' for now.

		// check file permissions.
		if (access(a->file_name, R_OK)) {
			dlog(DLOG_WARNING, "CON: permission denied for file: '%s'\n", a->file_name);
			httperror(c->fd, 403, NULL, NULL);
			return(0);
		}

		dlog(DLOG_DEBUG, "CON: serving '%s' f:%lu @%dx%d\n",a->file_name,a->frame,a->out_width,a->out_height);
	} else {
		httperror(c->fd, 400, "Bad Request", "<p>Can not parse query parameters.</p>"
		//"<h3>Help</h3>"
		// TODO: short usage
		//"<h3>Example</h3>"
		//"<p>http://localhost:1554/?frame=70&w=600&h=-1&file=robin.avi&format=png</p>" //c->d->hostname:port
		);
	}
	return doit;
}

///////////////////////////////////////////////////////////////////
/// LAZY param evaluation
#if 0
static char *parse_query2_param(char *kvp, const char *key) {
	char *sep;
	if (!(sep=strchr(kvp,'='))) return NULL;
	*sep='\0';
	char *val = sep+1;
	if (!val || strlen(val) < 1 || strlen(kvp) <1) {
		*sep='=';
		return NULL;
	}

	//dlog(DLOG_INFO, "QUERY '%s'->'%s'\n",kvp,val);

	if (!strcmp (kvp, key)) {
		*sep='=';
		return val;
	}
	*sep='=';
	return NULL;
}

char *parse_http_query2(char *query, const char *k) {
	char *t, *s = query;
	char *rv=NULL;
	/* TODO cache values on 1st parse ?! */
	while(!rv && s && (t=strpbrk(s,"&?"))) {
		*t='\0';
		rv=parse_query2_param(s,k);
		if (rv) rv=strdup(rv);
		*t='&';
		s=t+1;
	}
	if (!rv && s) {
		rv=parse_query2_param(s,k);
		if (rv) rv=strdup(rv);
	}
	return rv;
}
#endif
/////////////////////////////////////////////////////////////////////
//

// see icsd.c
int hardcoded_video(int fd, httpheader *h, ics_request_args *a);
char *server_status_html (CONN *c);
char *session_info (CONN *c, ics_request_args *a);
void ics_clear_cache();

// see httpindex.c
char *index_dir (const char *root, char *base_url, const char *path, int opt);

/////////////////////////////////////////////////////////////////////
//

/** main http request handler / dispatch requests */
void ics_http_handler(
		CONN *c,
		char *host, char *protocol,
		char *path, char *method_str,
		char *query, char *cookie
		) {

	if (CTP("/status")) {
		char *status = server_status_html(c);
		SEND200(status);
		free(status);
		c->run=0;
	} else if (CTP("/favicon.ico")) {
		#include "favicon.h"
		httpheader h;
		memset(&h, 0, sizeof(httpheader));
		h.ctype="image/x-icon";
		h.length=sizeof(favicon_data);
		http_tx(c->fd, 200, &h, sizeof(favicon_data), favicon_data);
		c->run=0;
	} else if (CTP("/info")) { /* /info -> /file/info !! */
		ics_request_args a;
		memset(&a, 0, sizeof(ics_request_args));
		if (parse_http_query(c, query, NULL, &a)) {
			char *info = session_info(c,&a);
			SEND200(info);
			free(info);
		}
		c->run=0;
	} else if (CTP("/index/")) { /* /index/  -> /file/index/ ?! */
		char *dp = url_unescape(&(path[7]), 0, NULL);
		if (!dp || check_path(dp)) {
			httperror(c->fd, 400, "Bad Request", "Illegal filename." );
		} else {
			char base_url[1024];
			snprintf(base_url,1024, "http://%s%s\n", host, path);
			char *msg = index_dir(c->d->docroot,base_url, dp, 0);
			SEND200(msg);
			free(dp);
			free(msg);
		}
		c->run=0;
	} else if (CTP("/admin")) { /* /admin/ */
		if (strncasecmp(path,  "/admin/flush_cache", 18) == 0 ) {
			ics_clear_cache();
			SEND200("ok");
		}	else if (strncasecmp(path,  "/admin/shutdown", 15) == 0 ) {
			SEND200("ok");
			c->d->run=0;
		} else {
			httperror(c->fd, 400, "Bad Request", "Nonexistant admin command." );
		}
		c->run=0;
	} else if (CTP("/") && !strcmp(path, "/") && strlen(query)==0) { /* HOMEPAGE */
		// TODO: check if xslt is avail -> custom home-page
		// otherwise: fall-back to build-in internal:
#define HPSIZE BUFSIZ // max size of homepage in bytes.
		char msg[HPSIZE]; int off =0;
		off+=snprintf(msg+off, HPSIZE-off, DOCTYPE HTMLOPEN);
		off+=snprintf(msg+off, HPSIZE-off, "<title>ICS</title></head>\n<body>\n<h2>ICS</h2>\n\n");
		off+=snprintf(msg+off, HPSIZE-off, "<p>Hello World,</p>\n");
		off+=snprintf(msg+off, HPSIZE-off, "<ul>");
		off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"status/\">Server Status</a></li>\n");
		off+=snprintf(msg+off, HPSIZE-off, "<li><a href=\"index/\">File Index</a></li>\n");
		off+=snprintf(msg+off, HPSIZE-off, "</ul>");
		off+=snprintf(msg+off, HPSIZE-off, "<hr/><p>sodankyla-ics/%s at %s:%i</p>", ICSVERSION, c->d->local_addr, c->d->local_port);
		off+=snprintf(msg+off, HPSIZE-off, "\n</body>\n</html>");
		SEND200(msg);
		c->run=0; // close connection
	} else if (CTP("/gui/")) {
		SEND200("NOT YET IMPLEMENTED"); // requires XML/XSLT
		c->run=0;
	} else if (CTP("/session/")) { // TODO: rename "/session/" to "/api/" ?!
		SEND200("Sessions are not supported in this version.");
		c->run=0;
	} else if (CTP("/stream")) { /* /stream -> /file/stream !! */
		SEND200("Stream re-encoding is not supported in this version.");
		c->run=0;
	}
	else if (  (strncasecmp(protocol,  "HTTP/", 5) == 0 ) /* /?file= -> /file/frame?.. !! */
			     &&(strcasecmp (method_str, "GET") == 0 )
			    )
	{
		ics_request_args a;
		httpheader h;
		memset(&a, 0, sizeof(ics_request_args));
		memset(&h, 0, sizeof(httpheader));

		// Note: parse_http_query()  sends httperror messages if needed.
		if ((parse_http_query(c, query, &h, &a)&3) == 3) {
			hardcoded_video(c->fd, &h, &a); // DO THE WORK
		}
		if (a.file_name) free(a.file_name);
		c->run=0; // close connection
	}
	else
	{
		httperror(c->fd,500, "", "server does not know what to make of this.\n");
		c->run=0;
	}
}
