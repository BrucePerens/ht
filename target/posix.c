#include "posix_driver.h"

bool
dummy_gpio(serial_context * const context)
{
  return true;
}

size_t
posix_read(serial_context * const context, char * const buffer, const size_t buffer_length)
{
  return 0;
}

size_t
posix_write(serial_context * const context, const char * const buffer, const size_t buffer_length)
{
  return 0;
}

void
posix_wait(const float seconds)
{
}

void
posix_wake()
{
}
