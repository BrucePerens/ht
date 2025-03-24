// Main
//
// Start the program. Initialize system facilities. Set up an event andler
// to call wifi_event_sta_start() once the WiFi station starts and is ready
// for configuration.
//
// Code in wifi.c starts the web server once WiFi is configured. All other
// facilities are started from the web server.
//
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include <esp_crt_bundle.h>
#include <esp_tls.h>
#include <esp_random.h>
#include <esp_console.h>
#include <esp_mac.h>
#include <bootloader_random.h>
#include <hal/efuse_hal.h>
#include "generic_main.h"

static const char TASK_NAME[] = "main";

bool	app_main_called = false;

static void initialize()
{
  // const unsigned int chip_revision = efuse_hal_chip_revision();

  // Set the console print lock, so that things in tasks don't print over each other.
  // This can't be used for non-tasks.
  pthread_mutex_init(&GM.console_print_mutex, 0);

  gm_wifi_events_initialize();

  // gm_improv_wifi(0);

  gm_user_initialize_early();

  // Initialize the TCP/IP stack. gm_select_task uses sockets.
  esp_netif_init();
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  close(fd);

  // The global event loop is required for all event handling to work.
  ESP_ERROR_CHECK(esp_event_loop_create_default());


  // Get the factory-set MAC address, which is a permanent unique number programmed
  // into e-fuse bits of this CPU, and thus is useful for identifying the device.
  esp_efuse_mac_get_default(GM.factory_mac_address);

  // This will be used for the name of the WiFi access point until the user sets the
  // host name.
  snprintf(
   GM.unique_name,
   sizeof(GM.unique_name),
   "%s-%02x%02x%02x%02x%02x%02x",
   GM.application_name,
   GM.factory_mac_address[0],
   GM.factory_mac_address[1],
   GM.factory_mac_address[2],
   GM.factory_mac_address[3],
   GM.factory_mac_address[4],
   GM.factory_mac_address[5]);

  gm_printf("Device name: %s\n", GM.unique_name);

  // Connect the non-volatile-storage FLASH partition. Initialize it if
  // necessary.
  esp_err_t err = nvs_flash_init();
  if ( err != ESP_OK ) {
    ESP_LOGW(TASK_NAME, "Erasing and initializing non-volatile parameter storage.");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }
  esp_err_t nvs_open_err = nvs_open(GM.nvs_index, NVS_READWRITE, &GM.nvs);

  if ( nvs_open_err != ESP_OK )
    gm_flash_failure("nvs open", nvs_open_err);

  // Create and store an AES key used for encrypting cookie data for
  // login security and other persistent data. This is less secure than
  // storing session data on the host, but this is a memory-constrained
  // environment with a finite number of FLASH write cycles.
  // The esp_fill_random() operation has to happen after WiFi is enabled,
  // as the hardware random generator uses noise from the high-speed ADC
  // for randomness.
  //
  uint8_t aes_key[32];
  size_t key_size = sizeof(aes_key);
  size_t iv_size = sizeof(GM.aes_cookie_iv);
  size_t hmac_key_size = sizeof(GM.hmac_key);
  const char key_name[] = "aes_key";
  const char iv_name[] = "cookie_iv";
  const char hmac_key_name[] = "hmac_key";

  const esp_err_t blob1_err = nvs_get_blob(GM.nvs, key_name, aes_key, &key_size);
  const esp_err_t blob2_err = nvs_get_blob(
   GM.nvs,
   iv_name,
   GM.aes_cookie_iv,
   &iv_size);
  const esp_err_t blob3_err = nvs_get_blob(
   GM.nvs,
   hmac_key_name,
   GM.hmac_key,
   &hmac_key_size);

  if ( blob1_err != ESP_OK
   ||  blob2_err != ESP_OK
   ||  blob3_err != ESP_OK
   ||  key_size != sizeof(aes_key) 
   ||  iv_size != sizeof(GM.aes_cookie_iv) 
   ||  hmac_key_size != sizeof(GM.aes_cookie_iv) 
   || *(unsigned long *)&aes_key == 0
   || *(unsigned long *)&GM.aes_cookie_iv == 0
   || *(unsigned long *)&GM.hmac_key == 0 ) {
    // WiFi is not yet started, as we need encryption first.
    // So, use the bootloader random source (one or more of the ADCs, Espressif
    // says thermal noise, but possibly including radio reception).
    bootloader_random_enable();
    esp_fill_random(aes_key, sizeof(aes_key));
    const esp_err_t set_key_err = nvs_set_blob(
     GM.nvs,
     key_name,
     aes_key,
     sizeof(aes_key));

    // We use the same AES initialization vector all of the time for cookie 
    // encryption, so the cookie plaintext _must_ contain randomness.
    esp_fill_random(GM.aes_cookie_iv, sizeof(GM.aes_cookie_iv));
    const esp_err_t set_iv_err = nvs_set_blob(
     GM.nvs,
     iv_name,
     GM.aes_cookie_iv,
     sizeof(GM.aes_cookie_iv));

    esp_fill_random(GM.hmac_key, sizeof(GM.hmac_key));
    const esp_err_t set_hmac_key_err = nvs_set_blob(
     GM.nvs,
     iv_name,
     GM.aes_cookie_iv,
     sizeof(GM.aes_cookie_iv));

    const esp_err_t nvs_commit_error = nvs_commit(GM.nvs);

    esp_err_t err;
    if ( (err = set_key_err)
     || (err = set_iv_err)
     || (err = set_hmac_key_err)
     || (err = nvs_commit_error) ) {
      (void)gm_flash_failure("nvs cryptographic keys", err);
    }
    bootloader_random_disable();
  }

  // Initialize a hardware AES context with the cookie encryption key.
  esp_aes_init(&GM.aes_cookie_context);
  esp_aes_setkey(&GM.aes_cookie_context, aes_key, 256);

  gm_select_task();
  gm_wifi_start();

#ifndef CONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME
  // The GDB stub uses the console, so don't run the interpreter if it's in use.
  gm_command_interpreter_start();
#endif
}

void app_main()
{
  if ( app_main_called )
    return;

  app_main_called = true;
  GM.log_file_pointer = stderr;
  initialize();
}
