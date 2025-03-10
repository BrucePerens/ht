#ifndef _PLATFORM_DOT_H_
#define _PLATFORM_DOT_H_
#include "radio.h"
#include "os_driver.h"

typedef bool (*gpio_ptr)(platform_context * context, unsigned long bits);

struct platform_context {
#ifdef DRIVER_posix
  int	fd;
#endif
  /*@shared@*/ gpio_ptr	gpio;
  /*@shared@*/ read_ptr	read;
  /*@shared@*/ write_ptr write;
  /*@shared@*/ wait_ptr	wait;
  /*@shared@*/ wake_ptr	wake;
};
typedef struct platform_context platform_context;

extern bool
platform_gpio(platform_context * const context, unsigned long bits);

extern platform_context /*@null@*/ /*@only@*/ *
platform_init(const char /*@null@*/ * filename) /*@globals errno;@*/;

extern void
platform_end(platform_context /*@only@*/ * platform);

#endif
