#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <lwip/sockets.h>
#include "generic_main.h"

// Tiny redirect server. Don't use the http server just to do redirect, it uses too many
// resources.

const char redirect[] = "\
HTTP/1.1 301 Moved Permanently\r\n\
Connection: close\r\n\
Content-Length: 25\
Content-Type: text/html; charset=UTF-8\r\n\
Location: https:\r\n\
\r\n\
Please use https: URLs.\r\n";

static void
accept_handler(int sock, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  struct sockaddr_storage	client_address;
  socklen_t			client_size = sizeof(client_address);

  if ( !readable || exception ) 
    return;

  const int connection = accept(sock, (struct sockaddr *)&client_address, &client_size);
  if ( connection < 0 ) {
    GM_FAIL("Select event server accept failed: %s\n", strerror(errno));
    return;
  }

  // FIX: Make everything from here on a non-blocking handler.

  char	buffer[1024];

  const ssize_t size = recv(connection, buffer, (socklen_t)sizeof(buffer), MSG_WAITALL);
  if ( size > 0 ) {
    // Parse the request.
  }
  
  if ( size == sizeof(buffer) ) {
    int status;
    while ( (status = recv(connection, buffer, sizeof(buffer), MSG_WAITALL)) > 0 );

    // If status isn't zero, the recv() threw an error, not just end-of-file, and the
    // connection is likely gone.
    if ( status == 0 )
      (void) send(connection, redirect, sizeof(redirect) - 1, 0);
  
    (void) shutdown(connection, SHUT_WR);
    (void) close(connection);
  }
}

esp_err_t
redirect_to_https()
{
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  int no = 0;     

  // FIX: Rewrite this to be an IPV6 socket.
  // socket so that it will accept both IPV4 and IPV6.
  const struct sockaddr_in serv_addr = {
   .sin_family = AF_INET,
   .sin_addr.s_addr = htonl(INADDR_ANY),
   .sin_port = htons(80)
  };

  if ( bind(listener, (const struct sockaddr *)&serv_addr, (socklen_t)sizeof(serv_addr)) < 0 )
    return ESP_FAIL;

  // Accept both IPV4 and IPV6 connections.
  (void) setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)); 

  if ( listen(listener, 10) < 0 )
    return ESP_FAIL;

  gm_fd_register(listener, accept_handler, 0, true, false, true, 0);
  return ESP_OK;
}
