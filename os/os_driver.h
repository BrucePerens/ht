#include <stdbool.h>
#include <stddef.h>
#include "radio.h"

size_t
os_read(platform_context * const context, char * const buffer, const size_t buffer_length);

size_t
os_write(platform_context * const context, const char * const buffer, const size_t buffer_length);

void
os_wait(const float seconds);

void
os_wake();
