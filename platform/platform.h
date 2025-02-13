#ifndef _PLATFORM_DOT_H_
#define _PLATFORM_DOT_H_
#include "radio.h"
#include "os_driver.h"

typedef bool (*gpio_ptr)(platform_context * context);

typedef struct platform_context {
#ifdef DRIVER_posix
  int	fd;
#endif
  /*@shared@*/ gpio_ptr	gpio;
  /*@shared@*/ read_ptr	read;
  /*@shared@*/ write_ptr	write;
  /*@shared@*/ wait_ptr	wait;
  /*@shared@*/ wake_ptr	wake;
} platform_context;

bool
platform_gpio(platform_context * const context);
#endif
