#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "generic_main.h"

int gm_pattern_string(const char * string, gm_pattern_coroutine_t coroutine, char * buffer, size_t buffer_size)
{
  char * out = buffer;
  const char * start = string;

  *buffer = '\0';

  // While there is an input string to process...
  while ( *start != '\0' ) {
    // Look for a replacement string in the form of {name}
    const char * var = index(start, '{');
    if (var) {
      // Found it. Find its end.
      const char * end = index(var, '}');
      if (end) {
        var++;
        if (var == end) {
          GM_FAIL("Replacement string name missing.");
          return -1;
        }
        else {
          const char * next_open = index(var, '{');
          if (next_open && next_open < end) {
            GM_FAIL("Missing '}' character in replacement string: %s", var - 1);
            return -1;
          }


	  // Copy the input string before the replacement pattern to the output.
          size_t leading_size = var - start - 1;
          if (leading_size > buffer_size - 1) {
            GM_FAIL("Out of space for replacement string.");
            return -1;
          }
          memcpy(out, start, leading_size);
          out += leading_size;
          buffer_size -= leading_size;

          // Make a null-terminated copy of the name, to use as an argument to the
          // coroutine. To conserve memory, this is done in the output buffer. Then
          // it is overwritten.
          size_t name_size = end - var;
          if (name_size > buffer_size - 1) {
            GM_FAIL("Replacement name too large, out of space in output string, or missing '}': %s", var - 1);
            return -1;
          }
          memcpy(out, var, name_size);
          out[name_size] = '\0';

          int status;
          // Call the coroutine, it writes the replacement directly to the output
          // buffer. Note that the output buffer is both the input and output.
          if ( (status = ((*coroutine)(out, out, buffer_size))) == 0 ) {
            size_t result_size = strlen(out);
            out += result_size;
            buffer_size -= result_size;
            start = end + 1;
          }
          else {
            GM_FAIL("No replacement for: %s, %d", out, status);
            return -1;
          }
        }
      }
      else {
        GM_FAIL("Missing '}' character in replacement string: %s", var);
        return -1;
      }
    }
    else {
      size_t final_length = strlen(start);

      if (final_length > buffer_size - 1) {
        GM_FAIL("Out of space for replacement string.");
        *buffer = '\0';
        return -1;
      }
      memcpy(out, start, final_length + 1);
      break;
    }
  }
  return 0;
}
