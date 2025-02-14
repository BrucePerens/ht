#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "radio_driver.h"
#include "os_driver.h"

int
main(int, char * *) /*@globals errno;@*/
{
  platform_context /*@null@*/ /*@owned@*/ * platform = platform_init("/dev/tty");

  if ( platform == 0 )
    return 1;

#ifdef DRIVER_sa818
  radio_module * module = sa818(platform);
  if ( module )
    (void) radio_end(module);
#endif

  platform_end(platform);
  return  0;
}
