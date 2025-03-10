#include <stdarg.h>
#include <stdio.h>
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "generic_main.h"

// Print to the console, with mutual exclusion.
// This is only usable in tasks, not anything at interrupt level.
// Event loops each run in their own task, so it's usable from event handlers.

void
gm_fail(const char * function, const char * file, int line, const char * pattern, ...)
{
  va_list args;

  va_start(args, pattern);
  gm_vprintf(pattern, args);
  va_end(args);

  // This isn't on GM.log_file_pointer.
  esp_backtrace_print(100);
  fflush(stderr);
}

int
gm_vprintf(const char * pattern, va_list args)
{
  int length;
  pthread_mutex_lock(&GM.console_print_mutex);
  length = vfprintf(GM.log_file_pointer, pattern, args);
  fflush(GM.log_file_pointer);
  pthread_mutex_unlock(&GM.console_print_mutex);
  return length;
}

int
gm_printf(const char * pattern, ...)
{
  int length;
  va_list args;
  va_start(args, pattern);
  length = gm_vprintf(pattern, args);
  va_end(args);
  return length;
}
