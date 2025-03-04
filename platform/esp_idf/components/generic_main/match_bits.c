#include "generic_main.h"
#include <stddef.h>
#include <stdint.h>

// Count the matching bits between two data until one doesn't match, then stop.
size_t
gm_match_bits(const void * const restrict av, const void * const restrict bv, size_t size) {
  typedef uint8_t type;
  const uint8_t * const restrict a = (const type *)av;
  const uint8_t * const restrict b = (const type *)bv;

  size_t count = 0;
  for (size_t i = 0; i < size; ++i) {
    int xor_result = a[i] ^ b[i];
    int j = 7;
    do {
      if (!((xor_result >> j) & 1))
        count++;
      else
        return count;
    } while ( j-- > 0 );
  }
  return count;
}
