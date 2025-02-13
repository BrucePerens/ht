#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "radio_driver.h"

int
main(int, char * *)
{
  platform_context platform;

#ifdef DRIVER_posix
  platform.fd = 0;
#endif

#ifdef DRIVER_sa818
  radio_module * module =
  sa818(
    &platform,
    platform_gpio,
    os_read,
    os_write,
    os_wait,
    os_wake
  );
  if ( module )
    (void) radio_end(module);
#endif
  return  0;
}
