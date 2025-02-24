// Web Server
//
// Operate an HTTP and HTTPS web server. Maintain the SSL server certificates.
//
#include <string.h>
#include <stdlib.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <inttypes.h>
#include "generic_main.h"

static const char TASK_NAME[] = "web_server";
static httpd_handle_t ssl_server = NULL;

static void time_was_synchronized(struct timeval * t)
{
  // The first time the clock is adjusted, it's changed immediately from the epoch
  // to the current time. This sets the SNTP code so that the second and subsequent
  // times, it is adjusted smoothly.
  if (GM.time_last_synchronized == 0) {
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    esp_sntp_restart();
  }
  GM.time_last_synchronized = esp_timer_get_time();
}

void start_webserver(void)
{
  if (ssl_server)
    return;

  // Simple redirect server without the memory overhead of starting an instance
  // of a full http server just to do redirects.
  gm_start_redirect_to_https();

  // Web servers need accurate time.
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(time_was_synchronized);
  // Adjust the clock suddenly the first time. The time_was_synchronized
  // callback will set it to be adjusted smoothly the second and subsequent
  // time.
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  // Re-adjust the clock every 10 minutes.
  esp_sntp_set_sync_interval(60 * 10 * 1000);

  esp_sntp_init();

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  // config.httpd.stack_size = 48 * 1024;
  config.httpd.core_id = 0;
  config.httpd.uri_match_fn = httpd_uri_match_wildcard;
  config.httpd.lru_purge_enable = true;
  gm_self_signed_ssl_certificates(&config);

  // Start the httpd server
  ESP_LOGI(TASK_NAME, "Starting server on port: %d", config.port_secure);
  if (httpd_ssl_start(&ssl_server, &config) == ESP_OK) {
    gm_web_handler_install(ssl_server);
  }
  else {
    ssl_server = NULL;
    ESP_LOGI(TASK_NAME, "Error starting server!");
  }
}

void stop_webserver()
{
  if (ssl_server) {
    gm_stop_redirect_to_https();
    esp_sntp_stop();
    GM.time_last_synchronized = 0;
    httpd_ssl_stop(ssl_server);
    ssl_server = NULL;
  }
}
