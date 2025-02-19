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
#include "generic_main.h"

static void initialize(void);

static const char TASK_NAME[] = "main";

bool	app_main_called = false;

void app_main(void)
{
  if ( app_main_called )
    return;

  app_main_called = true;
  GM.log_file_pointer = stderr;
  initialize();
}

static void initialize(void)
{
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

  // Connect the non-volatile-storage FLASH partition. Initialize it if
  // necessary.
  esp_err_t err = nvs_flash_init();
  if ( err != ESP_OK ) {
    ESP_LOGW(TASK_NAME, "Erasing and initializing non-volatile parameter storage.");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }
  ESP_ERROR_CHECK(nvs_open(GM.nvs_index, NVS_READWRITE, &GM.nvs));

  // Get the factory-set MAC address, which is a permanent unique number programmed
  // into e-fuse bits of this CPU, and thus is useful for identifying the device.
  esp_efuse_mac_get_default(GM.factory_mac_address);

  // This will be used for the name of the WiFi access point until the user sets the
  // host name.
  snprintf(
   GM.unique_name,
   sizeof(GM.unique_name),
   "%s-%02x:%02x:%02x:%02x:%02x:%02x",
   GM.application_name,
   GM.factory_mac_address[0],
   GM.factory_mac_address[1],
   GM.factory_mac_address[2],
   GM.factory_mac_address[3],
   GM.factory_mac_address[4],
   GM.factory_mac_address[5]);

  gm_printf("Device name: %s\n", GM.unique_name);

  gm_select_task();
  gm_wifi_start();

  // Create and store an AES key used for encrypting cookie data for
  // login security and other persistent data. This is less secure than
  // storing session data on the host, but this is a memory-constrained
  // environment with a finite number of FLASH write cycles. This has to
  // happen after WiFi is enabled, as the hardware random generator uses
  // noise from the high-speed ADC for randomness.
  size_t size = sizeof(GM.aes_key);
  const char name[] = "aes_key";

  const esp_err_t blob_err = nvs_get_blob(GM.nvs, name, &GM.aes_key, &size);

  if ( blob_err != ESP_OK || size != sizeof(GM.aes_key) 
   || *(unsigned long *)&GM.aes_key == 0 ) {
    gm_printf("Writing\n");
    esp_fill_random(GM.aes_key, sizeof(GM.aes_key));
    const esp_err_t set_err = nvs_set_blob(GM.nvs, name, GM.aes_key, sizeof(GM.aes_key));
    if ( set_err == ESP_OK ) {
      const esp_err_t commit_err = nvs_commit(GM.nvs);
      gm_printf("Committed: %d\n", commit_err);
      size = sizeof(GM.aes_key);
      const esp_err_t get_err = nvs_get_blob(GM.nvs, name, GM.aes_key, &size);
      gm_printf("Get after write: %d, key %lx:%lx:%lx:%lx",
      get_err,
      *(unsigned long *)&GM.aes_key[0],
      *(unsigned long *)&GM.aes_key[1],
      *(unsigned long *)&GM.aes_key[2],
      *(unsigned long *)&GM.aes_key[3]);
    }
  }
  else {
    gm_printf("Got stored key: %d, key %lx:%lx:%lx:%lx",
    blob_err,
    *(unsigned long *)&GM.aes_key[0],
    *(unsigned long *)&GM.aes_key[1],
    *(unsigned long *)&GM.aes_key[2],
    *(unsigned long *)&GM.aes_key[3]);
  }

#ifndef CONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME
  // The GDB stub uses the console, so don't run the interpreter if it's in use.
  gm_command_interpreter_start();
#endif
}
