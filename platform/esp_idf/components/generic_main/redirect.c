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

// Tiny redirect server, just redirects to the HTTPS root. This allows us to
// avoid using the full http server, with its memory overhead, just to do
// redirect. This one shares a thread and stack with all of the other GM
// select-based event functions.

const char redirect[] = "\
HTTP/1.1 301 Moved Permanently\r\n\
Connection: close\r\n\
Content-Length: 25\r\n\
Content-Type: text/html; charset=UTF-8\r\n\
Location: https://192.168.10.206/\r\n\
\r\n\
Please use https: URLs.\r\n";

static void
io_handler(int sock, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  if ( !readable || exception || timeout ) {
    // Client went away or is unacceptably slow.
     gm_printf("Hanging up on the client.\n");
    (void) shutdown(sock, SHUT_RDWR);
    (void) close(sock);
    return;
  }

  gm_printf("Receive started.\n");
  const ssize_t size = recv(sock, buffer, (socklen_t)sizeof(buffer), MSG_DONTWAIT);
  gm_printf("Receive finished.\n");
  
  if ( size >= 0 ) {
    gm_printf("Send.\n");
    (void) send(sock, redirect, sizeof(redirect) - 1, 0);
  }
  else {
    gm_printf("Receive returned %d %s\n", size, strerror(errno));
  }
  (void) shutdown(sock, SHUT_RDWR);
  (void) close(sock);
}

static void
accept_handler(int sock, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  struct sockaddr_storage	client_address;
  socklen_t			client_size = sizeof(client_address);

  if ( !readable || exception ) 
    return;

  gm_printf("Accept handler.\n");
  const int connection = accept(
   sock,
   (struct sockaddr *)&client_address,
   &client_size);
  if ( connection < 0 ) {
    GM_FAIL("Select event server accept failed: %s\n", strerror(errno));
    return;
  }
  gm_fd_register(connection, io_handler, 0, true, false, true, 30);
}

static int listener = -1;

esp_err_t
gm_start_redirect_to_https()
{
  listener = socket(AF_INET6, SOCK_STREAM, 0);
  const int no = 0;     

  // socket so that it will accept both IPV4 and IPV6.
  const struct sockaddr_in6 serv_addr = {
   .sin6_family = AF_INET6,
   // lwip defines struct in6_addr and associated things a bit differently than
   // other platforms.
   .sin6_addr = IN6ADDR_ANY_INIT,
   .sin6_port = htons(80)
  };

  gm_printf("Created socket.\n"); 
  if ( bind(listener, (const struct sockaddr *)&serv_addr, (socklen_t)sizeof(serv_addr)) < 0 ) {
    gm_printf("Bind failed.\n");
    return ESP_FAIL;
  }

  // Accept both IPV4 and IPV6 connections.
  (void) setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&no, sizeof(no)); 

  if ( listen(listener, 10) < 0 ) {
    gm_printf("Listen failed.\n");
    return ESP_FAIL;
  }

  gm_fd_register(listener, accept_handler, 0, true, false, true, 0);
  return ESP_OK;
}

void
gm_stop_redirect_to_https()
{
  if ( listener >= 0 ) {
    gm_fd_unregister(listener);
    shutdown(listener, SHUT_RDWR);
    close(listener);
    listener = -1;
  }
}
