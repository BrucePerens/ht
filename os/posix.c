#include "posix_driver.h"

size_t
posix_read(platform_context * const context, char * const buffer, const size_t buffer_length)
{
  return 0;
}

size_t
posix_write(platform_context * const context, const char * const buffer, const size_t length)
{
  return write(context->fd, buffer, length);
}

void
posix_wait(const float seconds)
{
}

void
posix_wake()
{
}
