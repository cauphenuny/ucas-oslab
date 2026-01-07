#ifndef INCLUDE_PRINTK_H_
#define INCLUDE_PRINTK_H_

#include <stdarg.h>
#include <type.h>

/* kernel print */
int printk(const char* fmt, ...);

/* kernel fast print */
int printkf(const char* fmt, ...);

/* vt100 print */
int printv(const char* fmt, ...);

/* (QEMU-only) save print content to logfile */
int printl(const char* fmt, ...);

int snprintf(char* str, size_t size, const char* fmt, ...);

#endif
