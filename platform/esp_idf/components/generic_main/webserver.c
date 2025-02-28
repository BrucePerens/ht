// Web Server
//
// Operate an HTTP and HTTPS web server. Maintain the SSL server certificates.
//
#include <string.h>
#include <stdlib.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <inttypes.h>
#include "generic_main.h"

static const char TASK_NAME[] = "web_server";
static httpd_handle_t ssl_server = NULL;

void start_webserver(void)
{
  if (ssl_server)
    return;

  // Simple redirect server without the memory overhead of starting an instance
  // of a full http server just to do redirects.
  gm_start_redirect_to_https();

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
    GM.time_last_synchronized = 0;
    httpd_ssl_stop(ssl_server);
    ssl_server = NULL;
  }
}
