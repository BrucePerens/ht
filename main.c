#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "radio_driver.h"
#include "os_driver.h"

platform_context /*@null@*/ /*@only@*/ *
platform_init(const char * filename)
{
  platform_context * platform = malloc(sizeof(*platform));
  if ( platform == 0 )
    return 0;

#ifdef DRIVER_posix
  platform->fd = 0;
#endif
  platform->gpio = platform_gpio;
  platform->read = os_read;
  platform->write = os_write;
  platform->wait = os_wait;
  platform->wake = os_wake;

  return platform;
}

void
platform_end(platform_context /*@only@*/ * platform)
{
  free(platform);
}

int
main(int, char * *)
{
  platform_context /*@null@*/ /*@owned@*/ * platform = platform_init("/dev/tty");

  if ( platform == 0 )
    return 1;

#ifdef DRIVER_sa818
  radio_module * module =
  sa818(platform);
  if ( module )
    (void) radio_end(module);
#endif

  platform_end(platform);
  return  0;
}
