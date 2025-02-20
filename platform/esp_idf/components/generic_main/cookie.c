#include <cjson.h>
#include <hwcrypto/aes.h>
#include "ascii85.h"

void
parse_cookie(httpd_req_c * const req)
{
  char		raw_cookie[1024];
  char		plaintext[1024];
  uint8_t	iv[16];
  size_t 	size = sizeof(raw_cookie);

  esp_err e = httpd_req_get_hdr_value_str(
   req,
   "Cookie",
   raw_cookie,
   sizeof(raw_cookie));
   const size_t length = strlen(raw_cookie);

  const size_t padding_length = 16 - ((sizeof(raw_cookie) - length) % 16);
  memset(iv, 0, sizeof(iv));
  memset(&raw_cookie[length], 0, padding);

  esp_aes_crypt_cbc(
   &GM.aes_ctx,
   ESP_AES_DECRYPT,
   length + padding,
   iv,
   (uint8_t *)raw_cookie,
   (uint8_t*)plaintext);

  const cJSON * const json = cJSON_Parse(plaintext);
}

void
write_cookie(httpd_req_c * const req, const cJSON * const json)
{
  char nonce[16];

  esp_fill_random(nonce, sizeof(nonce));
  // Do I have to free this?
  char * json = cJSON_Print(json);
  char plaintext[1024];
  {
    char raw_cookie[sizeof(plaintext)];
    const size_t raw_length = strlen(json);
    if ( raw_length > sizeof(raw_cookie) - 1 )
      return; // FIX: Throw an error here.
    strncpy(raw_cookie, sizeof(raw_cookie), json);
    free(json);
  
    const size_t padding = 16 - ((sizeof(raw_cookie) - raw_length) % 16);
    memset(&raw_cookie[length], 0, padding);
  
    const int aes_size = esp_aes_crypt_cbc(
     &GM.aes_ctx,
     ESP_AES_DECRYPT,
     raw_length + padding,
     nonce,
     (uint8_t *)raw_cookie,
     (uint8_t*)plaintext);

     // raw_cookie[] is out of scope once this block ends.
     // We reuse the memory.
  }

  char encoded_cookie[((size_t)(sizeof(raw_cookie) * 1.25)) + 4u];
  strncpy(encoded_cookie, sizeof(encoded_cookie), "c=");

  const int32_t ascii85_size = encode_ascii85(
   plaintext,
   raw_length + padding,
   &encoded_cookie[2],
   sizeof(encoded_cookie) - 2);
  httpd_resp_set_hdr(req, "Set-Cookie", encoded_cookie);
}
