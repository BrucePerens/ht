#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "generic_main.h"

const char nvs_name[] = "user";

esp_err_t
gm_get_user_data(const char * name)
{
  // Use cached user data, the request generally will be from the same user
  // as last time, and FLASH reads are slow.
  if ( GM.user_name && strcmp(name, GM.last_user_name) == 0 ) {
    return ESP_OK;
  }
  else {
    if ( GM.user_name != NULL )
      free(GM.user_name
    GM.user_name = ;
  }

  nvs_handle_t * nvs = 0;

  const open_err = nvs_open(nvs_name, NVS_READ, &nvs);
  // This will fail quietly if no user has been defined.
  if ( open_err != ESP_OK ) { 
    if ( open_err == ESP_ERR_NVS_NOT_INITIALIZED )
      return err;
    else
      return gm_flash_failure(nvs_name, err);
  }

  size_t size = sizeof(GM.user_data);
  esp_err_t err = nvs_get_blob(nvs, name, GM.user_data, &size);
  (void) nvs_close(nvs_name);

  if ( err != ESP_OK ) {
    if ( err != ESP_ERR_NVS_NOT_FOUND )
      gm_flash_failure(nvs_name, err);
    return err;
  }

  if ( size != sizeof(user) ) {
    // Figure out what to do here if the size of the structure changes.
    // For now, just quit.
    gm_printf("User structure size incorrect.\n");
    return ESP_FAIL;
  }

  GM.user_name = stralloc(user);
  return err;
}
