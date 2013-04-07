/* compile with
 *
 * gcc -o libharvid_example libharvid_example.c \
 *   -I../libharvid/ ../libharvid/libharvid.a \
 *   `pkg-config --cflags --libs libavcodec libavformat libswscale libavutil`
 *
 */

#include <stdio.h>
#include <harvid.h>

/* ffmpeg decoder honors these externs */
int want_verbose = 0;
int want_quiet   = 1;

/* dlog implementation required by libharvid */
#ifndef NDEBUG
int debug_section = 0;
#endif
int debug_level  = 0;

void dlog(int level, const char *format, ...) {}


/* this example decodes frame #5 of the given video-file
 * and write a ppm image to '/tmp/libharvid_example.ppm'
 */

void dothework(void *dc, void *vc, const char *file_name) {
  unsigned short vid;   // video id
  void *cptr = NULL;    // pointer to cacheline
  uint8_t *bptr = NULL; // decoded video-frame data
  VInfo ji; // video file info
  int err = 0;

  int decode_fmt = PIX_FMT_RGB24; // see libavutil.h
  int64_t frame = 5; // video-frame to decode -- start counting at zero.

	/* get (or create) numeric ID for given file */
  vid = dctrl_get_id(vc, dc, file_name);

  jvi_init(&ji);
  /* get canonical output width/height and corresponding buffersize
	 * width,height == 0 -> original width, no-scaling
	 */
  if ((err=dctrl_get_info_scale(dc, vid, &ji,
					/*out_width*/ 0 , /*out_height*/ 0, decode_fmt))
			|| ji.buffersize < 1)
	{
		/* no decoder can be found, or the decoder can not provide information */
    if (err == 503)
		{
			fprintf(stderr, "503/try again -- Service Temporarily Unavailable.\nNo decoder is available. The server is currently busy or overloaded.\n");
		}
		else
		{
			fprintf(stderr, "500/service unavailable -- No decoder is available\nFile is invalid (no video track, unknown codec, invalid geometry,..)\n");
		}
		return;
	}

	/* get frame from cache - or decode it into the cache
	 *
	 * Note: the 'cache' provides a zero-copy buffer to both
	 * the decoder-backend as well as to this user-code.
	 * -> it is not possible to bypass the cache.
	 */
	bptr = vcache_get_buffer(vc, dc, vid, frame, ji.out_width, ji.out_height, decode_fmt, &cptr, &err);

	if (!bptr)
	{
		/* an error occured while decoding the frame */
		if (err == 503)
		{
			fprintf(stderr, "503/try again -- Service Temporarily Unavailable\nVideo cache is unavailable. The server is currently busy or overloaded.\n");
		}
		else
		{
			fprintf(stderr, "500/service unavailable -- No decoder or cache is available\nFile is invalid (no video track, unknown codec, invalid geometry,..)\n");
		}
		return;
	}

	/* DO SOMETHING WITH THE VIDEO DATA */

	const char * outfn = "/tmp/libharvid_example.ppm";
	printf("writing ppm image to '%s' (%d bytes RGB, %dx%d)\n",
			outfn, ji.buffersize, ji.out_width, ji.out_height);

	FILE *x = fopen(outfn, "w");
  fprintf(x, "P6\n%d %d\n255\n", ji.out_width, ji.out_height);
  fwrite(bptr, ji.out_height, 3*ji.out_width, x);
  fclose(x);


	/* tell cache we're not using data at bptr (in cacheline cptr) anymore */
	vcache_release_buffer(vc, cptr);
}

int main (int argc, char **argv) {
	const int cache_size = 128;
	const int max_decoder_threads = 8;
	void *dc = NULL; // decoder control
	void *vc = NULL; // video frame cache

  /* initialize */
  ff_initialize();
  vcache_create(&vc);
  vcache_resize(&vc, cache_size);
  dctrl_create(&dc, max_decoder_threads, cache_size);


	/* now multiple threads can access the decoder and cache
	 * simultaneously. There can be more user-threads than
	 * max_decoder_threads ..
	 *
	 * However, in this example we don't use pthreads..
	 */
	dothework(dc, vc, "/tmp/test.avi");


  /* cleanup */
  vcache_destroy(&vc);
  dctrl_destroy(&dc);
  ff_cleanup();

	return 0;
}
