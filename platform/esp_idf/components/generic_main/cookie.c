// unsigned char * base64_decode(const char *src, size_t len, size_t *out_len);
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cJSON.h>
#include <sys/param.h>
#include <aes/esp_aes.h>
#include <esp_random.h>
#include <esp_https_server.h>
#include <esp_rom_crc.h>
#include <esp_log.h>
#include "../src/utils/base64.h"
#include "generic_main.h"

// We use JSON for session context. It gets sent to the browser as an encrypted 
// cookie, and the browser sends it back. On a machine with more resources, we
// would keep the session context on a disk, or at least a larger FLASH with
// better wear-leveling. ESP-32 has small FLASH with a limited number of write
// cycles, and its wear-leveling depends on the presence of _unused_ space on the
// partition, which of course is limited by the FLASH size. Once you ruin the
// FLASH the device is probably bricked. So don't store session context locally.

#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif

// C won't let me define array sizes at file scope with a number in a const size_t.
#define PAYLOAD_SIZE 1024

const char tag[] = "cookie";

// Our cookie is called "c".
const uint8_t cookie_start[] = "c=";

// Send cookie only on SSL, expire it after 30 days, try to avoid cross-site
// scripting. The device renews the cookie if you keep using it.
const uint8_t cookie_end[] =
"; Secure; SameSite=Strict; Max-Age=2592000; Partitioned;";

// This buffer gets used for two different things in write_cookie(), to save stack
// on the very memory-constrained ESP-32.
union CookiePayload {
  // Struct payload contains compact JSON, with a header.
  struct Payload {
    struct Header {
      // This checksum is used to verify that we have decrypted the payload
      // data successfully, and to prevent bit-manipulation of the
      // cookie as a means of breaking our encryption.
      uint32_t crc32;

      // Cookie encryption always uses the same AES initialization vector,
      // so that we don't have to send the IV as a separate cookie.
      // Thus, we _must_ include randomness in the payload so that the
      // encrypted output is not the same every time for the same JSON input,
      // which would provide information for breaking our encryption.
      uint32_t random;
    } header;

    char data[roundup(PAYLOAD_SIZE + sizeof(struct Header), 16)];
  } payload;

  // Struct Cookie contains the Set-Cookie datum.
  struct Cookie {
    // This contains cookie_start[] without the terminating null.
    uint8_t start[sizeof(cookie_start) - 1];

    // This contains the base64-encoded encrypted JSON data.
    uint8_t data[
     sizeof(struct Payload)
     + (sizeof(struct Payload) / 3) + 3 // Account for Base64-encoding.
     + sizeof(cookie_end)];
  } cookie;

  // Typed access to encrypt struct Payload.
  uint8_t bytes[sizeof(struct Cookie)];

  // Typed access to send struct Cookie as the Set-Cookie datum.
  char characters[sizeof(struct Cookie)];
};

cJSON *
gm_read_cookie(httpd_req_t * req)
{
  union CookiePayload b;
  char a[sizeof(b.characters)];
  size_t data_length = sizeof(a);
  
  esp_err_t cookie_err = httpd_req_get_cookie_val(req, "c", (char *)a, &data_length);

  // The most likely error here is simply that the current request did not
  // contain our session data cookie.
  if ( cookie_err == ESP_ERR_NOT_FOUND )
    return 0; // Fail quietly.
  else if ( cookie_err != ESP_OK ) {
    gm_printf("Cookie error: %s.\n", esp_err_to_name(cookie_err));
    return 0;
  }

  // Decode from Base-64 and get the encrypted payload.
  size_t length = 0;
  uint8_t * const unbased = base64_decode(a, data_length, &length);
  memcpy(a, unbased, length);
  a[length] = '\0';
  free(unbased);

  if ( length % 16 > 0 ) {
    gm_printf("Cookie payload length %d is not a multiple of 16.\n", length);
    return 0;
  }

  // AES modifies the IV so that you can use it to stream. So, make a temporary
  // copy.
  uint8_t iv[sizeof(GM.aes_cookie_iv)];
  memcpy(iv, GM.aes_cookie_iv, sizeof(iv));
  
  // Decrypt the payload.
  (void) esp_aes_crypt_cbc(
   &GM.aes_cookie_context,
   ESP_AES_DECRYPT,
   length,
   iv,
   (uint8_t *)a,
   b.bytes);

  // Check that the calculated CRC for the data matches the one in the cookie.
  const uint32_t crc32 = esp_rom_crc32_le(
   0,
   (uint8_t *)&b.payload.header.random, length - sizeof(b.payload.header.crc32));

  if ( b.payload.header.crc32 != crc32 ) {
    gm_printf("Cookie CRC doesn't match.\n");
    return 0;
  }
  
  // Parse the payload.
  cJSON * volatile json = cJSON_ParseWithLength(
   b.payload.data,
   length - sizeof(b.payload.header));

  if ( json == NULL ) {
    gm_printf("Cookie JSON parse failed.\n");
    return 0;
  }

  return json;
}

void
gm_write_cookie(httpd_req_t * const req, cJSON * const json)
{
  // Allow for a 1024 bytes of compact JSON. This effects the necessary httpd
  // header size, and the httpsd stack size.
  // The http to https redirector doesn't use cookies, and these cookies are only
  // sent to https connections.
  // The + 8 is to account for the size of the header in struct Payload.
  uint8_t a[PAYLOAD_SIZE + sizeof(struct Header)];
  union CookiePayload b;

  if ( cJSON_PrintPreallocated(
   json,
   b.payload.data,
   sizeof(b.payload.data),
   0) == 0 ) {
    ESP_LOGE(tag, "JSON context too large for buffer.\n");
    sleep(1);
    abort();
  }

  const size_t length = strlen((const char *)b.payload.data);

  // Because we always use the same initialization vector for AES,
  // add randomness to make the plaintext unpredictable. This avoids
  // having to send the initialization vector in another cookie.
  esp_fill_random(&b.payload.header.random, sizeof(b.payload.header.random));


  // Calculate padding, because AES will only process data of a size that
  // is a multiple of 16.
  const size_t padding = roundup(length + sizeof(b.payload.header), 16)
   - (length + sizeof(b.payload.header));
  memset(&b.payload.data[length], 0, padding);

  // Add a 32-bit checksum to the encrypted content, to defeat bit manipulation
  // and to verify successful decryption. We checksum the padding because when
  // we decrypt the payload, it includes the padding and it's difficult to figure
  // out the length of the padding in the read function without a size field.
  b.payload.header.crc32 = esp_rom_crc32_le(
   0,
   (uint8_t *)&b.payload.header.random,
   sizeof(b.payload.header.random) + length + padding);

  // AES modifies the IV so that you can use it to stream. So, make a temporary
  // copy.
  uint8_t iv[sizeof(GM.aes_cookie_iv)];
  memcpy(iv, GM.aes_cookie_iv, sizeof(iv));

  // Encrypt the payload.
  (void) esp_aes_crypt_cbc(
   &GM.aes_cookie_context,
   ESP_AES_ENCRYPT,
   length + sizeof(b.payload.header) + padding,
   iv,
   b.bytes,
   a);

  // Base-64-encode the encrypted payload, because the Set-Cookie datum can't
  // contain binary data or characters important to the parse like space or
  // semicolon.
  size_t base64_length = 0;
  char * const encoded = base64_encode(
   a,
   sizeof(b.payload.header) + length + padding,
   &base64_length);

  // Construct the Set-Cookie datum.
  memcpy(b.cookie.start, cookie_start, sizeof(b.cookie.start));
  // base64_encode adds a newline character, don't include it.
  base64_length--;
  memcpy(b.cookie.data, encoded, base64_length);
  free(encoded);
  // This includes null-termination.
  memcpy(&b.cookie.data[base64_length], cookie_end, sizeof(cookie_end));

  httpd_resp_set_hdr(req, "Set-Cookie", b.characters);
}

void
cookie_test()
{
}
