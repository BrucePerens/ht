#ifndef _OS_DRIVER_DOT_H_
#define _OS_DRIVER_DOT_H_
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#include "radio.h"

struct platform_context;
typedef struct platform_context platform_context;

typedef ssize_t (* read_ptr)(platform_context * const context, char * const buffer, const size_t buffer_length) /*@globals errno;@*/;
typedef ssize_t (*write_ptr)(platform_context * const context, const char * const buffer, const size_t buffer_length) /*@globals errno;@*/;
typedef void (*wait_ptr)(platform_context * const context, const float seconds);
typedef void (*wake_ptr)(platform_context * const context);

bool
os_open(platform_context * platform, const char * const filename) /*@globals errno;@*/;

extern ssize_t
os_read(platform_context * const context, char * const buffer, const size_t buffer_length) /*@globals errno;@*/;

extern ssize_t
os_write(platform_context * const context, const char * const buffer, const size_t buffer_length) /*@globals errno;@*/;

extern void
os_wait(platform_context * const context, const float seconds);

extern void
os_wake(platform_context * const context);
#endif
