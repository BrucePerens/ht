#include <string.h>
#include <stdlib.h>
#include <esp_http_server.h>
#include "generic_main.h"

esp_err_t frogfs_file_handler(httpd_req_t * const req, const gm_uri * const uri);

static esp_err_t
http_root_handler(httpd_req_t *req)
{
  // Redirect to /index.html
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_status(req, "301 Moved Permanently");
  httpd_resp_set_hdr(req, "Location", "/index.html");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t
http_file_handler(httpd_req_t * const req)
{
  gm_session(req);
  gm_uri uri = {};

  if ( gm_uri_parse(req->uri, &uri) != 0 )
    return ESP_ERR_INVALID_ARG;

  size_t length = strlen(uri.path);

  // Remove some common directory access nits before constructing a redirect path.
  while ( length > 0 ) {
    if ( length > 1 && uri.path[length - 1] == '/' ) {
      uri.path[length - 1] = '\0';
      length--;
    }
    else if ( length >= 2
     && uri.path[length - 2] == '/'
     && uri.path[length - 1] == '.' ) {
       uri.path[length - 2] = '\0';
       length -= 2;
    }
    else
      break;
  }

  // Run the built-in web handlers first. Don't allow them to be overriden
  // with a file, or the user might put their device in a situation that is
  // difficult to recover from (at least remotely) by overriding some system
  // service.
  if ( gm_web_handler_run(req, &uri, GET) == 0 )
    return ESP_OK;

  if ( frogfs_file_handler(req, &uri) == ESP_OK )
    return ESP_OK;

  // FIX: Test and enable.
  // req->uri = "/404.html";
  // return http_file_handler(req);

  // We get here if the file isn't found.
  httpd_resp_send_404(req);
  return ESP_OK;
}

void
gm_get_handlers(httpd_handle_t server)
{
  static const httpd_uri_t root = {
      .uri       = "/",
      .method    = HTTP_GET,
      .handler   = http_root_handler,
      .user_ctx  = NULL
  };
  httpd_register_uri_handler(server, &root);

  static const httpd_uri_t file = {
      .uri       = "/*",
      .method    = HTTP_GET,
      .handler   = http_file_handler,
      .user_ctx  = NULL
  };
  httpd_register_uri_handler(server, &file);
}
