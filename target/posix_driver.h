#include <stdbool.h>
#include <stddef.h>
#include "radio.h"

bool
dummy_gpio(serial_context * const context);

size_t
posix_read(serial_context * const context, char * const buffer, const size_t buffer_length);

size_t
posix_write(serial_context * const context, const char * const buffer, const size_t buffer_length);

void
posix_wait(const float seconds);

void
posix_wake();
