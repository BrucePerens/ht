#include "platform.h"

platform_context *
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
platform_end(platform_context * platform)
{
  free(platform);
}
