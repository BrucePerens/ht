#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <lwip/sockets.h>
#include <netinet/in.h>
#include "generic_main.h"

// Simple HTTP web server, just redirects to HTTPS.
// This uses less than 1700 bytes of ROM when built for debugging, and shares its
// stack with all of our other select-event-driven tasks. This allows us
// to avoid using the full http server, which uses up a thread and stack
// just to do redirect. All other services are via HTTPS, which has a full
// server with its own thread and stack.

const char redirect[] = "\
HTTP/1.1 301 Moved Permanently\r\n\
Content-Length: 0\r\n\
Content-Type: text/html; charset=UTF-8\r\n\
Location: https://%s/%s\r\n\r\n";

const char bad_request[] = "\
HTTP/1.1 400 Bad Request\r\n\r\n";

const char crlf[] = "\r\n";

struct header {
  const char *	name;
  const char *	data;
};

struct request {
  const char *	path;
  const char *  host;
  char *	next;
  const char *	end;
  bool		keep_alive;
  char		raw[CONFIG_HTTPD_MAX_REQ_HDR_LEN];
};

// The buffer was initialized to all zeros, so we can count on a terminating null.
static void
parse_request(struct request * const r, size_t size) {
  const char * const first_line = strstr(r->raw, crlf);
  if ( first_line == NULL )
    return;
  const char * first_space = strchr(r->raw, ' ');
  if ( first_space == NULL )
    return;

  r->path = first_space + 1;

  const char * s = first_line + 2;
  do {
    if ( strcmp(s, "\r\n") == 0 ) {
      r->end = s;
      break;
    }
    const char * const colon_space = strstr(s, ": ");
    if ( colon_space == NULL )
      break;

    const char * const line_end = strstr(colon_space, crlf);
    if ( line_end == NULL )
      break;

    if ( strncasecmp(s, "Connection: keep-alive", line_end - s) == 0 ) {
      r->keep_alive = true;
    }
    else if ( strncasecmp(s, "Host: ", 6) == 0 ) {
      r->host = colon_space + 2;
    }
    s = line_end + 2;
  } while ( s < r->next );
}

static void
send_bad_request(int sock, const struct request * r)
{
  gm_printf("Redirect: Bad request:\n%s.\n", r->raw);
  (void) send(sock, bad_request, sizeof(bad_request) - 1, 0);
}

static void
io_handler(int sock, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  struct request * const r = (struct request *)data;
  const size_t space = &r->raw[sizeof(r->raw) - 2] - r->next;

  if ( space > 0 && readable && !exception && !timeout ) {
    const ssize_t size = recv(
     sock,
     r->next,
     space,
     MSG_DONTWAIT);

    if ( size > 0 ) {
      r->next += size;

      parse_request(r, r->next - r->raw);
      if ( r->end ) {
        char response[256];

        if ( r->path && r->host ) {
          // These are guaranteed to work or r->path and r->host would not be set.
          *strchr(r->path, ' ') = '\0';
          *strstr(r->host, crlf) = '\0';

          const size_t response_length =
           strlen(r->host)
           + strlen(r->path)
           + sizeof(redirect) - 5;

          if ( response_length < sizeof(response) ) {
            snprintf(response, sizeof(response), redirect, r->host, r->path);
            (void) send(sock, response, response_length, 0);
            if ( r->keep_alive ) {
              // Handle another request from the same host.
              memset(r, 0, sizeof(*r));
              r->next = r->raw;
              return;
            }
          }
          else 
            send_bad_request(sock, r);
        }
        else
          send_bad_request(sock, r);
      }
      else
        return;
    }
    else if ( size == 0 && r->next != r->raw ) {
      if ( r->end == NULL || r->host == NULL || r->path == NULL )
        send_bad_request(sock, r);
    }
  }
  send_bad_request(sock, r);

  gm_fd_unregister(sock);
  (void) shutdown(sock, SHUT_RDWR);
  (void) close(sock);
  free(data);
  return;
}

static void
accept_handler(int sock, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  struct sockaddr_storage	client_address;
  socklen_t			client_size = sizeof(client_address);

  if ( !readable || exception ) 
    return;

  const int connection = accept(
   sock,
   (struct sockaddr *)&client_address,
   &client_size);
  if ( connection < 0 ) {
    GM_FAIL("Select event server accept failed: %s\n", strerror(errno));
    return;
  }
  struct request * r = (struct request *)malloc(sizeof(struct request));
  
  if ( r ) {
    memset(r, 0, sizeof(*r));
    r->next = r->raw;
    gm_fd_register(connection, io_handler, r, true, false, true, 30);
  }
  else {
    gm_printf("Redirect: out of memory.\n");
  }
}

static int listener = -1;

esp_err_t
gm_start_redirect_to_https()
{
  if ( listener >= 0 )
    return ESP_OK;

  listener = socket(AF_INET6, SOCK_STREAM, 0);
  const int no = 0;     

  const struct sockaddr_in6 serv_addr = {
   .sin6_family = AF_INET6,
   // lwip defines struct in6_addr and associated things a bit differently than
   // other platforms.
   .sin6_addr = IN6ADDR_ANY_INIT,
   .sin6_port = htons(80)
  };

  if ( bind(listener, (const struct sockaddr *)&serv_addr, (socklen_t)sizeof(serv_addr)) < 0 ) {
    gm_printf("Redirect: Bind failed.\n");
    return ESP_FAIL;
  }

  // Accept both IPV4 and IPV6 connections.
  (void) setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&no, sizeof(no)); 

  if ( listen(listener, 10) < 0 ) {
    gm_printf("Redirect: Listen failed.\n");
    return ESP_FAIL;
  }

  gm_fd_register(listener, accept_handler, 0, true, false, true, 0);
  return ESP_OK;
}

void
gm_stop_redirect_to_https()
{
  if ( listener < 0 )
    return;
  gm_fd_unregister(listener);
  shutdown(listener, SHUT_RDWR);
  close(listener);
  listener = -1;
}
