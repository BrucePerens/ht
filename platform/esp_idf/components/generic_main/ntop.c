#include <lwip/sockets.h>

void
gm_ntop(const struct sockaddr_storage * const s, char * const buffer, const size_t size)
{
  switch ( s->ss_family ) {
  case AF_INET:
    inet_ntop(s->ss_family, &((struct sockaddr_in *)s)->sin_addr, buffer, size);
    break;
  case AF_INET6:
    inet_ntop(s->ss_family, &((struct sockaddr_in6 *)s)->sin6_addr, buffer, size);
    break;
  }
}
