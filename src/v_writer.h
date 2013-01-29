#ifndef _writer_H
#define _writer_H
#include "jv.h"
enum {OUT_FMT_JPEG=1, OUT_FMT_PNG=2, OUT_FMT_PPM=3};

void write_image(JVARGS *ja, JVINFO *ji, uint8_t *buf);
long int format_image(uint8_t **out, JVARGS *ja, JVINFO *ji, uint8_t *buf);

#endif
