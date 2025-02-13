#include <unistd.h>
#include "os_driver.h"
#include "platform.h"

bool
os_open(platform_context * const, const char * const)
{
  errno = 0;
  return true;
}

ssize_t
os_read(platform_context * const context, char * const buffer, const size_t length)
{
  return read(context->fd, buffer, length);
}

ssize_t
os_write(platform_context * const context, const char * const buffer, const size_t length)
{
  return write(context->fd, buffer, length);
}

void
os_wait(platform_context * const, const float)
{
}

void
os_wake(platform_context * const)
{
}
