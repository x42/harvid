#ifndef _XJFF_H
#define _XJFF_H

#include <stdint.h>
#include "jv.h"

void ff_create(void **ff);
void ff_destroy(void **ff);
void ff_get_info(void *ptr, JVINFO *i);

void ff_render(void *ptr, unsigned long frame,
    uint8_t* buf, int w, int h, int xoff, int xw, int ys);

int ff_open_movie(void *ptr, JVARGS *a);
int ff_close_movie(void *ptr);

void ff_initialize (void);
void ff_cleanup (void);

uint8_t *ff_get_bufferptr(void *ptr);
uint8_t *ff_set_bufferptr(void *ptr, uint8_t *buf);
void ff_resize(void *ptr, int w, int h, uint8_t *buf, JVINFO *i);
#endif
