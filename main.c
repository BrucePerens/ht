#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "radio.h"
#include "os_driver.h"
#include "platform.h"

int
main(int, char * *)
{
  union platform_context platform;
#ifdef DRIVER_sa818
  radio_module * module =
  radio_sa818(
    &platform,
    dummy_gpio,
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
