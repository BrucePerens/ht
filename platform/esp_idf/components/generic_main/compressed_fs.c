#include <string.h>
#include <stdlib.h>
#include <esp_http_server.h>
#include "generic_main.h"

static const uint32_t	maximum_chunk_size = 4096;

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

static void
send_chunks(httpd_req_t * req, const char * data, uint32_t size)
{
  while ( size > 0 ) {
    uint32_t chunk_size = size;

    if ( chunk_size > maximum_chunk_size )
      chunk_size = maximum_chunk_size;

    httpd_resp_send_chunk(req, data, chunk_size);
    size -= chunk_size;
    data += chunk_size;
  }
  httpd_resp_send_chunk(req, "", 0);
}

static esp_err_t
http_file_handler(httpd_req_t *req)
{
  gm_uri uri = {};

  if ( gm_uri_parse(req->uri, &uri) != 0 )
    return ESP_ERR_INVALID_ARG;

  // If we get here, the file was not found.
  if ( gm_web_handler_run(req, &uri, GET) == 0 ) {
    // We found a handler for this URL. Return OK.
    return ESP_OK;
  }
  else {
    httpd_resp_send_404(req);
    return ESP_OK;
  }
}

void
gm_fs_web_handlers(httpd_handle_t server)
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
