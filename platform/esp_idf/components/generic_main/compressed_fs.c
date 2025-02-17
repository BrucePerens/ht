#include <string.h>
#include <stdlib.h>
#include <esp_http_server.h>
#include "generic_main.h"
#include <frogfs/frogfs.h>

extern const uint8_t frogfs_bin[];
extern const size_t frogfs_bin_len;
frogfs_fs_t * fs = 0;

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

static bool
client_accepts_compression(httpd_req_t * const req, const char * const type)
{
  char	buffer[128];

  if ( httpd_req_get_hdr_value_str(req, "Accept-Encoding", buffer, sizeof(buffer)) == ESP_OK ) {
    const size_t length = strlen(type);
    const char * found = strstr(buffer, type);

    if ( found ) {
      switch ( found[length] ) {
      case ',':
      case ';':
      case '\0':
      case '\r':
      case '\n':
        return true;
      }
    }
  }
  return false;
}

static esp_err_t
http_file_handler(httpd_req_t * const req)
{
  // Brotli decompression on esp-32 would require too much memory.
  const char * const compression = "deflate";

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

  const frogfs_entry_t * const e = frogfs_get_entry(fs, uri.path);
  if ( e ) {
    frogfs_stat_t s;
    frogfs_stat(fs, e, &s);
    
    if ( s.type == FROGFS_ENTRY_TYPE_DIR ) {
      // Redirect to index.html
      static const char index_name[] = "/index.html";
      size_t length = strlen(uri.path);
      char new_path[256];

      if ( length > sizeof(new_path) - sizeof(index_name) )
        return 

      snprintf(new_path, sizeof(new_path), "%s%s", uri.path, index_name);

      httpd_resp_set_type(req, "text/html");
      httpd_resp_set_status(req, "301 Moved Permanently");
      httpd_resp_set_hdr(req, "Location", new_path);
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    }

    frogfs_fh_t * const fh = frogfs_open(fs, e, 0);
    if ( fh ) {
      if ( s.compression == FROGFS_COMP_ALGO_ZLIB
       && client_accepts_compression(req, compression) ) {
        // Send compressed file.
        const void * void_data = 0;
  
        size_t size = frogfs_access(fh, &void_data);

        {
          char size_buffer[16];
          snprintf(size_buffer, sizeof(size_buffer), "%u", size);
          httpd_resp_set_hdr(req, "Content-Length", size_buffer);
        }
  
        if ( size > 0 ) {
          const char * data = void_data; // For pointer arithmetic.
    
          httpd_resp_set_hdr(req, "Content-Encoding", compression);

          while ( size > 0 ) {
            size_t io_size = size;
            if ( io_size > maximum_chunk_size )
              io_size = maximum_chunk_size;
    
            if ( httpd_resp_send_chunk(req, data, io_size) != ESP_OK )
              break; // Client hung up.
            size -= io_size;
            data += io_size;
          }
        }
      }
      else {
        // Send uncompressed file.
        size_t	size = s.size;
    
        {
          char size_buffer[16];
          snprintf(size_buffer, sizeof(size_buffer), "%u", size);
          httpd_resp_set_hdr(req, "Content-Length", size_buffer);
        }
  
        while ( size > 0 ) {
          // Here's where we use 4K of the stack, which is only 8K.
          char		buffer[maximum_chunk_size];
          size_t	io_size = size;

          if ( io_size > maximum_chunk_size )
  	    io_size = maximum_chunk_size;

          if ( frogfs_read(fh, buffer, io_size) == io_size ) {
            if ( httpd_resp_send_chunk(req, buffer, io_size) != ESP_OK )
              break; // Client hung up.
            size -= io_size;
          }
          else
            break; // Can't perform the IO, shouldn't happen.
        }
      }
      httpd_resp_send_chunk(req, "", 0);
      frogfs_close(fh);
      return ESP_OK;
    }
  }

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
  static const frogfs_config_t frogfs_config = {
    .addr = frogfs_bin,
  };

  fs = frogfs_init(&frogfs_config);

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
