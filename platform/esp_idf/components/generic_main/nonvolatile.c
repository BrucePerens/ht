#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "generic_main.h"

typedef struct gm_nonvolatile {
  const char * 		name;
  gm_nonvolatile_type_t	type;
  bool			secret;
  const char *		explanation;
  void			(*call_after_set)(void);
} gm_nonvolatile_t;

extern void gm_wifi_restart(void);

static const gm_nonvolatile_t gm_nonvolatile[] = {
  { "admin_password", STRING, true, "The admin user password.\n", 0},
  { "callsign", STRING, false, "Amateur Radio callsign.\n", 0},
  { "aprs_destination", STRING, false, "Destination for APRS packets, usually WIDE1.\n", 0},
  { "ddns_basic_auth", STRING, false, "send HTTP basic authentication on the first transaction with the Dynamic DNS server.\n", 0},
  { "ddns_hostname", STRING, false, "Hostname for this device to set in dynamic DNS.", 0 },
  { "ddns_password", STRING, true, "Password for secure access to the dynamic DNS host.", 0 },
  { "ddns_provider", STRING, false, "Name of the Dynamic DNS provider.", 0 },
  { "ddns_token", STRING, true, "secret token to set in dynamic DNS.", 0 },
  { "ddns_username", STRING, false, "User name for secure access to the dynamic DNS host.", 0 },
  { "ssid", STRING, false, "Name of the WiFi access point", gm_wifi_restart },
  { "timezone", STRING, false, "Time zone, like PST8PDT, (see https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv)", 0 },
  { "wifi_password", STRING, true, "Password of the WiFi access point", gm_wifi_restart },
  { }
};

esp_err_t
gm_flash_failure(const char * module, esp_err_t err) {
  if ( GM.flash_failure == ESP_OK )
    GM.flash_failure = err;

  GM_WARN_ONCE("FLASH memory: %s: %s\n", module, esp_err_to_name(err));
  return err;
}

void
gm_nonvolatile_list(gm_nonvolatile_list_coroutine_t coroutine)
{
  const gm_nonvolatile_t * p = gm_nonvolatile;
  char buffer[1024];
  size_t buffer_size;

  while (p->type) {
    buffer_size = sizeof(buffer);
    esp_err_t err = nvs_get_str(GM.nvs, p->name, buffer, &buffer_size);
    if ( err != ESP_OK ) {
      if ( err == ESP_ERR_NVS_NOT_FOUND ) {
        *buffer = '\0';
        (*coroutine)(p->name, buffer, p->explanation, GM_NOT_SET);
      }
      else
        gm_flash_failure("nvs", err);
    }
    else if ( p->secret ) {
      *buffer = '\0';
      (*coroutine)(p->name, buffer, p->explanation, GM_SECRET);
    }
    else
      (*coroutine)(p->name, buffer, p->explanation, GM_NORMAL);
    p++;
  }
}

gm_nonvolatile_result_t
gm_nonvolatile_get(const char * key, char * buffer, size_t buffer_size)
{
  const gm_nonvolatile_t * p = gm_nonvolatile;
  while (p->type) {
    if (strcmp(p->name, key) == 0)
      break;
    p++;
  }

  if (!p->type) {
    return GM_NOT_IN_PARAMETER_TABLE;
  }

  esp_err_t err = nvs_get_str(GM.nvs, key, buffer, &buffer_size);

  if (err) {
    if ( err != ESP_ERR_NVS_NOT_FOUND )
      gm_flash_failure("nvs", err);
    *buffer = '\0';
    return GM_NOT_SET;
  }

  if (p->secret)
    return GM_SECRET;
  else
    return GM_NORMAL;
}

gm_nonvolatile_result_t
gm_nonvolatile_set(const char * key, const char * value)
{
  const gm_nonvolatile_t * p = gm_nonvolatile;
  while (p->type) {
    if (strcmp(p->name, key) == 0)
      break;
    p++;
  }
  if (!p->type) {
    return GM_NOT_IN_PARAMETER_TABLE;
  }

  esp_err_t err = (nvs_set_str(GM.nvs, key, value));
  if (err) {
    gm_flash_failure("nvs", err);
    return GM_ERROR;
  }

  ESP_ERROR_CHECK(nvs_commit(GM.nvs));

  if (p->call_after_set)
    (p->call_after_set)();

  return GM_NORMAL;
}

gm_nonvolatile_result_t
gm_nonvolatile_erase(const char * key)
{
  const gm_nonvolatile_t * p = gm_nonvolatile;
  while (p->type) {
    if (strcmp(p->name, key) == 0)
      break;
    p++;
  }
  if (!p->type) {
    return GM_NOT_IN_PARAMETER_TABLE;
  }

  esp_err_t err = (nvs_erase_key(GM.nvs, key));

  switch (err) {
  case ESP_OK:
    ESP_ERROR_CHECK(nvs_commit(GM.nvs));
    if (p->call_after_set)
      (p->call_after_set)();
    return GM_NORMAL;
  case ESP_ERR_NVS_NOT_FOUND:
    return GM_NOT_IN_PARAMETER_TABLE;
  default:
    ESP_ERROR_CHECK(err);
    return GM_ERROR;
  }
}
