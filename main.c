#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "radio.h"
#ifdef DRIVER_posix
#include "posix_driver.h"
#endif

static union serial_context s_context;

int
main(int, char * *)
{
#ifdef DRIVER_posix
#ifdef DRIVER_sa818
  radio_module * module =
  radio_sa818(
    &s_context,
    dummy_gpio,
    posix_read,
    posix_write,
    posix_wait,
    posix_wake
  );
  if ( module )
    (void) radio_end(module);
#endif
#endif
  return  0;
}
