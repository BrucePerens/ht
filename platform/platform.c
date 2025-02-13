#include "platform.h"

/// \relates platform_context
/// Initialize the platform resources. This generally opens the radio device,
/// and sets up the functions to access it.
///
platform_context *
platform_init(const char * filename)
{
  platform_context /*@out@*/ * platform = malloc(sizeof(*platform));
  if ( platform == 0 )
    return 0;

  memset(platform, 0, sizeof(*platform));

  platform->gpio = platform_gpio;
  platform->read = os_read;
  platform->write = os_write;
  platform->wait = os_wait;
  platform->wake = os_wake;

  bool success;

  if ( filename )
    success = os_open(platform, filename);
  else {
    /// Platform-dependent: figure out the name of the radio device here.
    const char * const found_name = "/dev/tty";
    success = os_open(platform, found_name);
  }
  if ( !success ) {
    platform_end(platform);
    return 0;
  }
  
  return platform;
}

/// \relates platform_context
/// Close and release the platform resources.
///
void
platform_end(platform_context * platform)
{
  free(platform);
}
