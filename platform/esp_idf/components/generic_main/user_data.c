#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "generic_main.h"

const char nvs_name[] = "user";

esp_err_t
gm_get_user_data(const char * name, gm_user_data_t * data)
{
  nvs_handle_t nvs = 0;

  const esp_err_t open_err = nvs_open(nvs_name, NVS_READONLY, &nvs);
  // This will fail quietly if no user has been defined.
  if ( open_err != ESP_OK ) { 
    if ( open_err == ESP_ERR_NVS_NOT_INITIALIZED )
      return open_err;
    else
      return gm_flash_failure(nvs_name, open_err);
  }

  size_t size = sizeof(gm_user_data_t);
  const esp_err_t err = nvs_get_blob(nvs, name, data, &size);
  (void) nvs_close(nvs);

  if ( err != ESP_OK ) {
    if ( err != ESP_ERR_NVS_NOT_FOUND )
      gm_flash_failure(nvs_name, err);
    return err;
  }

  if ( size != sizeof(gm_user_data_t) ) {
    // Figure out what to do here if the size of the structure changes.
    // For now, just quit.
    gm_printf("User structure size incorrect.\n");
    return ESP_FAIL;
  }

  return ESP_OK;
}
