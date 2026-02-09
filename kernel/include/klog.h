#ifndef KLOG_H
#define KLOG_H

#include "types.h"

void klog_init_fb(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height);
void klog_enable(int enabled);
void klog_info(const char* msg);
void klog_error(const char* msg);

#define KLOG(msg) klog_info(msg)
#define KERR(msg) klog_error(msg)

#endif /* KLOG_H */
