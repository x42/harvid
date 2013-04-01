/* ffmpeg decoder honors these externs */
int want_verbose = 0;
int want_quiet   = 1;

/* dlog implementation required by libharvid */
#ifndef NDEBUG
int debug_section = 0;
#endif
int debug_level  = 0;

void dlog(int level, const char *format, ...) {}
