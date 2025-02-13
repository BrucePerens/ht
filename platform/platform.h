#ifndef _PLATFORM_DOT_H_
#define _PLATFORM_DOT_H_
#include "radio.h"

union platform_context {
#ifdef DRIVER_posix
  int	fd;
#endif
};
typedef union platform_context platform_context;

typedef bool (*gpio_ptr)(platform_context * context);

bool
platform_gpio(platform_context * const context);
#endif
