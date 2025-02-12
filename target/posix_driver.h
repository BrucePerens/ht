#include <stdbool.h>
#include <stddef.h>
#include "radio.h"

bool
dummy_gpio(platform_context * const context);

size_t
posix_read(platform_context * const context, char * const buffer, const size_t buffer_length);

size_t
posix_write(platform_context * const context, const char * const buffer, const size_t buffer_length);

void
posix_wait(const float seconds);

void
posix_wake();
