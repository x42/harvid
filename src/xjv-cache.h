#ifndef _XJCD_H
#define _XJCD_H

#include <stdlib.h>
#include <stdint.h>

void *get_decoder(int id); // client needs to provide this function

void cache_create(void **p);
void cache_destroy(void **p);
void cache_resize(void **p, int size);
uint8_t *cache_get_buffer(void *p, int id, unsigned long frame, int w, int h);
void dumpcacheinfo(void *p); // dump debug info to stdout
size_t formatcacheinfo(void *p, char *m, size_t n); // write HTML to m - max length n

void xjv_clear_cache (void *p);
/*
void cache_render(void *p, unsigned long frame,
    uint8_t* buf, int w, int h, int xoff, int xw, int ys);

// TODO cache invalidate. lock/unlock

*/
#endif
