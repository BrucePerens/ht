#include <stdint.h>
#include <sys/types.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include "generic_main.h"

typedef enum _nat_pmp_opcode {
  NAT_PMP_ANNOUNCE = 0,
  NAT_PMP_MAP_UDP = 1,
  NAT_PMP_MAP_TCP = 2
} nat_pmp_opcode_t;

typedef enum _nat_pmp_response_code {
  NAT_PMP_SUCCESS = 0,
  NAT_PMP_UNSUPP_VERSION = 1,
  NAT_PMP_NOT_AUTHORIZED = 2,
  NAT_PMP_NETWORK_FAILURE = 3,
  NAT_PMP_OUT_OF_RESOURCES = 4,
  NAT_PMP_UNSUPPORTED_OPCODE = 5
} nat_pmp_response_code_t;

typedef enum _pcp_opcode {
  PCP_ANNOUNCE = 0,
  PCP_MAP = 1,
  PCP_PEER = 2,
  PCP_RESPONSE = 0x80
} pcp_opcode_t;

typedef enum _pcp_version {
  NAT_PMP = 0,
  PORT_MAPPING_PROTOCOL = 2
} pcp_version_t;

// Ugh. The PCP response codes really should have included the first 5 NAT-PMP
// response codes with the same values. They just include the first 3.
typedef enum _pcp_result_code {
  PCP_SUCCESS = 0,
  PCP_UNSUPP_VERSION = 1,
  PCP_NOT_AUTHORIZED = 2,
  PCP_MALFORMED_REQUEST = 3,
  PCP_UNSUPP_OPCODE = 4,
  PCP_UNSUPP_OPTION = 5,
  PCP_MALFORMED_OPTION = 6,
  PCP_NETWORK_FAILURE = 7,
  PCP_NO_RESOURCES = 8,
  PCP_UNSUPP_PROTOCOL = 9,
  PCP_USER_EX_QUOTA = 10,
  PCP_CANNOT_PROVIDE_EXTERNAL = 11,
  PCP_ADDRESS_MISMATCH = 12,
  PCP_EXCESSIVE_REMOTE_PEERS = 13
} pcp_result_code_t;

typedef union _pcp_address {
  struct in6_addr sin6_addr;
  struct _ipv4_to_ipv6_mapping {
    uint8_t all_zeroes[10];
    uint8_t all_ones[2];
    in_addr_t s_addr;
  } ipv4_to_ipv6_mapping;
} pcp_address_t;

// For security reasons, implicit structure padding must not be allowed in
// structures that are communicated outside of a program.
// Under the C17 standard, implicit structure padding is not guaranteed to
// be initialized or copied, uninitialized data can leak information from
// previously-used memory. In practice, compilers generally do copy and
// initialize implicit padding.
#pragma pack(1)
typedef struct _nat_pmp_or_pcp {
  pcp_version_t version:8;
  pcp_opcode_t opcode:8; // Also contains the request/response bit.
  uint8_t reserved;
  pcp_result_code_t result_code:8;
  union {
    struct nat_pmp_packet {
      struct nat_pmp_request {
        uint16_t	internal_port;
        uint16_t	external_port;
        uint32_t	lifetime;
      } request;
      struct nat_pmp_response {
        uint32_t	epoch;
        uint16_t	internal_port;
        uint16_t	external_port;
        uint32_t	lifetime;
      } response;
    } nat_pmp;
    struct pcp_packet {
      uint32_t	lifetime;
      union {
        struct pcp_request {
          pcp_address_t client_address;
        } request;
        struct pcp_response {
          uint32_t	epoch;
          uint32_t	reserved1[3];
        } response;
      };
      union {
        struct pcp_map_peer {
          // For MAP or PEER opcode.
          gm_pcp_nonce_t nonce;
          uint8_t	protocol;
          uint8_t	reserved[3];
          uint16_t	internal_port;
          uint16_t	external_port;
          pcp_address_t external_address;
          // For PEER opcode only.
          uint16_t	remote_peer_port;
          uint16_t	reserved1;
          pcp_address_t	remote_peer_address;
        } mp;
      };
    } pcp;
  };
} nat_pmp_or_pcp_t;
#pragma pack()

// MAP and PEER are sent from any port on the host to 5351 on the router.
// ANNOUNCE is multicast from 5351 on the router to 5350 on a host.
enum pcp_port {
  PCP_SERVER_PORT = 5351,
  PCP_CLIENT_PORT = 5350
};
const size_t pcp_max_payload = 1100;
const size_t pcp_map_packet_size = (size_t)&(((nat_pmp_or_pcp_t *)0)->pcp.mp.remote_peer_port);
const size_t pcp_announce_packet_size = (size_t)&(((nat_pmp_or_pcp_t *)0)->pcp.mp);

static bool send_request(const nat_pmp_or_pcp_t * const, const struct sockaddr * const, const int, size_t);
static gm_pcp_nonce_t random_nonce();
static void renew_all_mappings_if_router_has_reset(const nat_pmp_or_pcp_t * const);
static void decode_pcp_announce(const nat_pmp_or_pcp_t * const, const ssize_t, const struct sockaddr_storage * const);
static void decode_pcp_map(nat_pmp_or_pcp_t *, ssize_t, struct sockaddr_storage *);
static void decode_pcp_peer(nat_pmp_or_pcp_t *, ssize_t, struct sockaddr_storage *);
static void incoming_packet(int, void *, bool, bool, bool, bool);
static void maintain_mappings();
static void remove_pcp_mapping(gm_port_mapping_t * *);
static void renew_all_mappings();
static void renew_ipv4(const gm_port_mapping_t * const);
static void renew_ipv6(const gm_port_mapping_t * const);
static void save_pcp_mapping(const nat_pmp_or_pcp_t * const, const gm_port_mapping_type_t);

static const int requested_mapping_duration = 15 * 60;
static const uint16_t https_port = 443;
static const uint16_t external_https_port = 7300;

// The "IPV4" socket is actually an IPV6 "ANY" address socket that it set to receive
// IPV4 packets as well. sendto() is used on it with an IPV4 address.
static int			ipv4_socket = -1;
// The IPV6 socket is bound to a specific address on the device, so that we don't
// use a global IPV6 address to speak to the local PCP server.
static int			ipv6_socket = -1;
static struct sockaddr_in6	my_ipv6_address = {
  .sin6_family = AF_INET6,
  .sin6_port = htons(PCP_CLIENT_PORT)
};
static uint32_t			last_received_epoch = 0;

// Assignable means of setting sockaddr_storage, so that I can make structures
// const. It takes IPV4-mapped-IPV6 addresses into account.
static struct sockaddr_storage
assign_sockaddr(const pcp_address_t * const a, const in_port_t port)
{
  const bool ipv4 = gm_all_zeroes(
   a->ipv4_to_ipv6_mapping.all_zeroes,
   sizeof(a->ipv4_to_ipv6_mapping.all_zeroes))
   && a->ipv4_to_ipv6_mapping.all_ones[0] == 0xff
   && a->ipv4_to_ipv6_mapping.all_ones[1] == 0xff;

  struct sockaddr_storage storage = {};

  if ( ipv4 ) {
    struct sockaddr_in * s = (struct sockaddr_in *)&storage;
    s->sin_family = AF_INET;
    s->sin_addr.s_addr = a->ipv4_to_ipv6_mapping.s_addr;
    s->sin_port = htons(port);
  }
  else {
    struct sockaddr_in6 * s = (struct sockaddr_in6 *)&storage;
    s->sin6_family = AF_INET6;
    s->sin6_addr = a->sin6_addr;
    s->sin6_port = htons(port);
  }
  return storage;
}
 
// Assignable version of gettimeofday(), so that I can make structures const.
static struct timeval
current_time()
{
  struct timeval t;
  gettimeofday(&t, 0);
  return t;
}

void
decode_packet(nat_pmp_or_pcp_t * p, ssize_t message_size, struct sockaddr_storage * address)
{
  bool		response;
  uint16_t	port;

  // This assumes all messages are PCP, we don't currently support NAT-PMP as
  // we'd only need it on really old routers.

  switch ( address->ss_family ) {
  case AF_INET:
    port = ntohs(((struct sockaddr_in *)address)->sin_port);
    break;
  case AF_INET6:
    port = ntohs(((struct sockaddr_in6 *)address)->sin6_port);
    break;
  default:
    GM_WARN_ONCE("PCP: decode_packet(): Address family %d.\n", address->ss_family);
    return;
  }

  if ( port != PCP_SERVER_PORT ) {
    GM_WARN_ONCE("PCP: Ignoring message that isn't from the PCP port.\n");
    return;
  }

  switch ( address->ss_family ) {
  case AF_INET:
    if ( ((struct sockaddr_in *)address)->sin_addr.s_addr != GM.sta.ip4.router.sin_addr.s_addr ) {
      GM_WARN_ONCE("IPV4 packet not from the router, ignored.\n");
      return;
    }
    break;
  case AF_INET6:
    if ( memcmp(
     &((struct sockaddr_in6 *)address)->sin6_addr,
     &GM.sta.ip6.router.sin6_addr,
     sizeof(GM.sta.ip6.router.sin6_addr)) != 0 ) {
      GM_WARN_ONCE("IPV6 packet not from the router, ignored.\n");
      return;
    }
    break;
  }

  response = p->opcode & PCP_RESPONSE;
  
  switch ( p->opcode & 0x7f ) {
  case PCP_ANNOUNCE:
    decode_pcp_announce(p, message_size, address);
    break;
  case PCP_MAP:
  case PCP_PEER:
    if ( !response ) {
      GM_WARN_ONCE("PCP client received request.\n");
      return;
    }
    switch ( p->opcode & 0x7f ) {
    case PCP_MAP:
      if ( message_size < pcp_map_packet_size ) {
        GM_WARN_ONCE("PCP receive packet too small for MAP: %d\n", message_size);
        return;
      }
      decode_pcp_map(p, message_size, address);
      break;
    case PCP_PEER:
      if ( message_size < sizeof(nat_pmp_or_pcp_t) ) {
        GM_WARN_ONCE("PCP receive packet too small for PEER: %d\n", message_size);
        return;
      }
      decode_pcp_peer(p, message_size, address);
      break;
    }
    break;
  default:
    GM_WARN_ONCE("PCP unrecognized opcode %x\n", p->opcode);
  }
}

static void
decode_pcp_announce(const nat_pmp_or_pcp_t * const p, const ssize_t message_size, const struct sockaddr_storage * const address)
{
  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received PCP Announce from %s.\n", buffer);
  renew_all_mappings_if_router_has_reset(p);
}

static void
decode_pcp_map(nat_pmp_or_pcp_t * p, ssize_t message_size, struct sockaddr_storage * address)
{
  char buffer[INET6_ADDRSTRLEN + 1];

  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received mapping from %s\n", buffer);

  // esp-idf has its own IPv6 address structure.
  esp_ip6_addr_t esp_addr = {};
  memcpy(esp_addr.addr, p->pcp.mp.external_address.sin6_addr.s6_addr, sizeof(esp_addr.addr));
  // Get the address type (global, link-local, etc.) for the IPv6 address.
  esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&esp_addr);

  gm_port_mapping_t * *	mp;
  if ( ipv6_type == ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6 )
   mp = &GM.sta.ip4.port_mappings;
  else
    mp = &GM.sta.ip6.port_mappings;

  while ( *mp ) {
    gm_port_mapping_t * const m = *mp;
    if ( memcmp(&p->pcp.mp.nonce, &m->nonce, sizeof(m->nonce)) == 0 )
      break;
    mp = &m->next;
  }

  if ( *mp == 0 ) {
    GM_WARN_ONCE("PCP: Received unrequested mapping, ignoring.\n");
    return;
  }

  if ( p->result_code != PCP_SUCCESS ) {
    GM_FAIL("PCP request failed, result code: %d.", p->result_code);
    
    remove_pcp_mapping(mp);
    return;
  }

  if ( ipv6_type != ESP_IP6_ADDR_IS_GLOBAL
   && ipv6_type != ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6) {
    inet_ntop(AF_INET6, p->pcp.mp.external_address.sin6_addr.s6_addr, buffer, sizeof(buffer));
    if ( memcmp(
     &p->pcp.mp.external_address.sin6_addr,
     &GM.sta.ip6.link_local.sin6_addr,
     sizeof(GM.sta.ip6.link_local.sin6_addr)) == 0 ) {
      GM_WARN_ONCE("Warning: The router responded to a PCP map request with a useless mapping specifying the link-local address of this device, instead of the global address of the router. This is probably a MiniUPnPd bug.\n");
    }
    else {
      GM_WARN_ONCE("Warning: The router responded to a PCP map request with a useless mapping to an IPv6 %s address, %s, instead of a global address. This is probably a MiniUPnPd bug.\n", GM.ipv6_address_types[ipv6_type], buffer);
    }
    remove_pcp_mapping(mp);
    return;
  }
  else {
    gm_port_mapping_t * const m = *mp;
    m->type = GM_GRANTED;
    if ( ipv6_type == ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6 ) {
      struct sockaddr_in * const s = (struct sockaddr_in *)&m->external;
      s->sin_family = AF_INET;
      s->sin_addr.s_addr = p->pcp.mp.external_address.ipv4_to_ipv6_mapping.s_addr;
      s->sin_port = ntohs(p->pcp.mp.external_port);
    }
    else {
      struct sockaddr_in6 * const s = (struct sockaddr_in6 *)&m->external;
      s->sin6_family = AF_INET6;
      s->sin6_addr = p->pcp.mp.external_address.sin6_addr;
      s->sin6_port = htons(p->pcp.mp.external_port);
    }
    gettimeofday(&m->granted_time, 0);
    m->expiration_time.tv_sec = m->granted_time.tv_sec + ntohl(p->pcp.lifetime) - 1;
    m->expiration_time.tv_usec = m->granted_time.tv_usec;
  }
}

static void
decode_pcp_peer(nat_pmp_or_pcp_t * p, ssize_t message_size, struct sockaddr_storage * address)
{
  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received PCP Peer from %s\n", buffer);
}

void
gm_pcp_request_mapping_ipv4()
{
  const nat_pmp_or_pcp_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .pcp.lifetime = htonl(requested_mapping_duration),

     // IPV6-mapped-IPV4 client address.
    .pcp.request.client_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .pcp.request.client_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,
    .pcp.request.client_address.ipv4_to_ipv6_mapping.s_addr = GM.sta.ip4.address.sin_addr.s_addr,

    // This sets the requested address to the all-zeroes IPV6-mapped-IPV4
    // address: ::ffff:0.0.0.0 .
    // If external_address was all zeroes, it would be the all-zeroes IPV6 address.
    .pcp.mp.external_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .pcp.mp.external_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,

    .pcp.mp.protocol = GM_PCP_TCP,
    .pcp.mp.internal_port = htons(https_port),
   
    .pcp.mp.nonce = random_nonce()
  };

  const struct sockaddr_in address = {
    .sin_addr.s_addr = GM.sta.ip4.router.sin_addr.s_addr,
    .sin_family = AF_INET,
    .sin_port = htons(PCP_SERVER_PORT)
  };

  if ( send_request(
   &p,
   (const struct sockaddr *)&address,
   ipv4_socket,
   sizeof(address)) )
    save_pcp_mapping(&p, GM_REQUEST);
}

void
gm_pcp_request_mapping_ipv6()
{
  const nat_pmp_or_pcp_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .pcp.lifetime = htonl(requested_mapping_duration),

    .pcp.request.client_address.sin6_addr = my_ipv6_address.sin6_addr,
    .pcp.mp.protocol = GM_PCP_TCP,
    .pcp.mp.internal_port = htons(https_port),
    .pcp.mp.external_port = htons(external_https_port),
    .pcp.mp.nonce = random_nonce()
  };

  const struct sockaddr_in6 address = {
    .sin6_family = AF_INET6,
    .sin6_addr = GM.sta.ip6.router.sin6_addr,
    .sin6_port = htons(PCP_SERVER_PORT)
  };

#if 0
  char				buffer[INET6_ADDRSTRLEN + 1];

  gm_ntop(&address, buffer, sizeof(buffer));
  gm_printf("Request IPv6 port mapping of router %s\n", buffer);
  inet_ntop(AF_INET6, &address.sin6_addr, buffer, sizeof(buffer));
  gm_printf("Client is %s\n", buffer);
#endif
 
  if ( send_request(
   &p,
   (const struct sockaddr *)&address,
   ipv6_socket,
   sizeof(address)) )
    save_pcp_mapping(&p, GM_REQUEST);
}

void
gm_pcp_start_ipv4()
{
  // FIX: Time-out responses and retry, eventually abandon trying.
  int			yes = 1;
  int			no = 0;

  if ( ipv4_socket >= 0 )
    return;

  const struct sockaddr_in6 address = {
   .sin6_family = AF_INET6,
   // lwip defines struct in6_addr and associated things a bit differently than
   // other platforms.
   .sin6_addr = IN6ADDR_ANY_INIT,
   .sin6_port = htons(PCP_CLIENT_PORT)
  };

  ipv4_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

  // Reuse addresses, because other software listens for all-hosts multicast.
  // Reuse the port, because ipv6_socket also listens upon this port
  // with a local address instead of the IPV6 ANY address.
  setsockopt(ipv4_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  setsockopt(ipv4_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
  // Accept both IPV4 and IPV6 connections.
  (void) setsockopt(ipv4_socket, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&no, sizeof(no)); 
  if ( bind(ipv4_socket, (struct sockaddr *)&address, sizeof(address)) < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Bind failed");
    return;
  }

  struct ip_mreq m = {
   .imr_interface.s_addr = GM.sta.ip4.address.sin_addr.s_addr
  };
  inet_pton(AF_INET, "224.0.0.1", &m.imr_multiaddr.s_addr);
  if ( setsockopt(ipv4_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) < 0 ) {
    // This fails because the host is already registered to the "all-hosts" multicast
    // group. Ignore that.
    if ( errno != EADDRNOTAVAIL ) {
      GM_FAIL_WITH_OS_ERROR("Setsockopt IP_ADD_MEMBERSHIP failed");
      return;
    }
  }
  struct ipv6_mreq m6 = {
    .ipv6mr_interface = 0 // Default.
  };
  inet_pton(AF_INET6, "ff02::1", &m6.ipv6mr_multiaddr.s6_addr);
  if ( setsockopt(ipv4_socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m6, sizeof(m6)) < 0 ) {
    // This fails because the host is already registered to the "all-hosts" multicast
    // group. Ignore that.
    if ( errno != EADDRNOTAVAIL ) {
      GM_FAIL_WITH_OS_ERROR("Setsockopt IP_ADD_MEMBERSHIP failed");
      return;
    }
  }
  gm_fd_register(ipv4_socket, incoming_packet, 0, true, false, true, 1);
}

// Don't start this until a router advertisement is received.
void
gm_pcp_start_ipv6()
{
  if ( ipv6_socket >= 0 )
    return;

  // Find the local address that has the most high bits that match the router's
  // address. Use that address to communicate with the router.
  struct in6_addr addresses[5] = { 
    GM.sta.ip6.link_local.sin6_addr,
    GM.sta.ip6.site_local.sin6_addr,
  };
  memcpy(&addresses[2], GM.sta.ip6.global, sizeof(*addresses) * 3);

  size_t bits = 0;

  char buf_b[128];
  inet_ntop(AF_INET6, &GM.sta.ip6.router.sin6_addr, buf_b, sizeof(buf_b));
  
  for ( size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); i++ ) {
    size_t this_bits = gm_match_bits(
     &addresses[i],
     &GM.sta.ip6.router.sin6_addr,
     sizeof(*addresses));

    if ( this_bits > bits ) {
      my_ipv6_address.sin6_addr = addresses[i];
      bits = this_bits;
    }
  }

  if ( bits == 0 ) {
    GM_FAIL("Didn't find a local address that matches the router.");
    return;
  }


  ipv6_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if ( ipv6_socket < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Can't get a socket");
  }

  // Make the address reusable, because other things have bound to this address.
  // Make the port reusable, because we listen to this port with a socket bound
  // to the IPV6 ANY address, and with this socket.
  int yes = 1;
  setsockopt(ipv6_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  setsockopt(ipv6_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

  if ( bind(ipv6_socket, (struct sockaddr *)&my_ipv6_address, sizeof(my_ipv6_address)) < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Bind failed");
    return;
  }
  gm_fd_register(ipv6_socket, incoming_packet, 0, true, false, true, 0);
}

void
gm_pcp_stop(void)
{
  // Close the sockets.
  if ( ipv4_socket >= 0 ) {
    gm_fd_unregister(ipv4_socket);
    shutdown(ipv4_socket, SHUT_RDWR);
    close(ipv4_socket);
    ipv4_socket = -1;
  }
  if ( ipv6_socket >= 0 ) {
    gm_fd_unregister(ipv6_socket);
    shutdown(ipv6_socket, SHUT_RDWR);
    close(ipv6_socket);
    ipv6_socket = -1;
  }

  // All I/O is shut down, so  at this point, there can be no
  // new port mappings. Remove the existing port mapping data.
  gm_port_mapping_t * m = GM.sta.ip4.port_mappings;
  GM.sta.ip4.port_mappings = 0;
  while ( m ) {
    gm_port_mapping_t * const next = m->next;
    free(m);
    m = next;
  }
  m = GM.sta.ip6.port_mappings;
  GM.sta.ip6.port_mappings = 0;
  while ( m ) {
    gm_port_mapping_t * const next = m->next;
    free(m);
    m = next;
  }
}

static void
incoming_packet(int fd, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  gm_printf("Incoming packet: readable: %d, writable: %d, exception: %d, timeout: %d\n", readable, writable, exception, timeout);

  if ( readable ) {
    nat_pmp_or_pcp_t		packet;
    struct sockaddr_storage	address;
    socklen_t			address_size = sizeof(address);
    ssize_t			message_size;
  
    message_size = recvfrom(fd, &packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&address, &address_size);
   
    if ( message_size <= 0 ) {
      GM_FAIL_WITH_OS_ERROR("recvfrom returned %d", message_size);
      return;
    }
  
  #if 0
    int port;
    char buffer[INET6_ADDRSTRLEN + 1];
    gm_ntop(&address, buffer, sizeof(buffer));
  
    gm_printf("PCP received %s packet of size %d from %s port %d.\n",
     data != 0 ? "multicast" : "unicast",
     message_size,
     buffer,
     port);
  #endif

    decode_packet(&packet, message_size, &address);
  }
  maintain_mappings();
}

static void
maintain_mappings()
{
  gm_printf("In maintain_mappings.\n");

  struct timeval now;
  gettimeofday(&now, 0);

  if ( ipv4_socket >= 0 ) {
    gm_port_mapping_t * m = GM.sta.ip4.port_mappings;
    while ( m ) {
      // *m is potentially changing, so save m->next before it does.
      gm_port_mapping_t * const next = m->next;
      if ( m->type == GM_GRANTED ) {
        if ( timercmp(&m->expiration_time, &now, >) ) {
          m->type = GM_REQUEST;
          renew_ipv4(m);
        }
        else {
          struct timeval remaining;
          timersub(&m->expiration_time, &now, &remaining);
          if ( remaining.tv_sec < m->lifetime / 3 )
            renew_ipv4(m);
        }
      }
      else {
        // This is a requested mapping that hasn't been confirmed by the server.
        renew_ipv4(m);
      }
      m = next;
    }
  }

  if ( ipv6_socket >= 0 ) {
    gm_port_mapping_t * m = GM.sta.ip6.port_mappings;
    while ( m ) {
      // *m is potentially changing, so save m->next before it does.
      gm_port_mapping_t * const next = m->next;
      if ( m->type == GM_GRANTED ) {
        if ( timercmp(&m->expiration_time, &now, >) ) {
          m->type = GM_REQUEST;
          renew_ipv6(m);
        }
        else {
          struct timeval remaining;
          timersub(&m->expiration_time, &now, &remaining);
          if ( remaining.tv_sec < 300 )
            renew_ipv6(m);
        }
      }
      else {
        // This is a requested mapping that hasn't been confirmed by the server.
        renew_ipv6(m);
      }
      m = next;
    }
  }
}

// Provide random nonce data in assignable form, so that I can make
// structures const.
static gm_pcp_nonce_t
random_nonce() {
  gm_pcp_nonce_t n;
  esp_fill_random(&n, sizeof(n));
  return n;
}

static void
remove_pcp_mapping(gm_port_mapping_t * * mp)
{
    gm_port_mapping_t * old = *mp;
    *mp = (*mp)->next;
    free(old);
}

static void
renew_all_mappings()
{
  gm_printf("In renew_all_mappings.\n");
  gm_port_mapping_t * m = GM.sta.ip4.port_mappings;

  if ( ipv4_socket < 0 )
    return;

  while ( m ) {
    // *m is potentially changing, so save m->next before it does.
    gm_port_mapping_t * const next = m->next;
    if ( m->type == GM_GRANTED )
      renew_ipv4(m);
    m = next;
  }

  if ( ipv6_socket < 0 )
    return;

  m = GM.sta.ip6.port_mappings;
  while ( m ) {
    gm_port_mapping_t * const next = m->next;
    if ( m->type == GM_GRANTED )
      renew_ipv6(m);
    m = next;
  }
}

static void
renew_all_mappings_if_router_has_reset(const nat_pmp_or_pcp_t * const p)
{
  if ( p->pcp.response.epoch < last_received_epoch ) {
    gm_printf("The router has reset. Renewing all PCP mappings.\n");
    last_received_epoch = p->pcp.response.epoch;
    renew_all_mappings();
  }
}

static void
renew_ipv4(const gm_port_mapping_t * const m)
{
  gm_printf("Renew ipv4\n");
  const struct sockaddr_in * const s = (struct sockaddr_in *)&m->external;

  const nat_pmp_or_pcp_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .pcp.lifetime = htonl(m->lifetime),

    .pcp.request.client_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .pcp.request.client_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,
  
    // PCP uses IPV4-mapped-IPV6 addresses.
    .pcp.mp.external_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .pcp.mp.external_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,
    .pcp.mp.external_address.ipv4_to_ipv6_mapping.s_addr = s->sin_addr.s_addr,
    .pcp.mp.external_port = htons(s->sin_port),
    .pcp.mp.protocol = m->protocol,
    .pcp.mp.internal_port = htons(m->internal_port),
    .pcp.mp.nonce = m->nonce
  };

  const struct sockaddr_in address = {
    .sin_addr.s_addr = GM.sta.ip4.router.sin_addr.s_addr,
    .sin_family = AF_INET,
    .sin_port = htons(PCP_SERVER_PORT)
  };

  send_request(&p, (struct sockaddr *)&address, ipv4_socket, sizeof(address));
}

static void
renew_ipv6(const gm_port_mapping_t * const m)
{
  struct sockaddr_in6 * s = (struct sockaddr_in6 *)&m->external;
  const nat_pmp_or_pcp_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .pcp.lifetime = htonl(m->lifetime),

    .pcp.request.client_address.sin6_addr = my_ipv6_address.sin6_addr,
    .pcp.mp.external_address.sin6_addr = s->sin6_addr,
    .pcp.mp.external_port = htons(s->sin6_port),
    .pcp.mp.protocol = m->protocol,
    .pcp.mp.internal_port = htons(m->internal_port),
    .pcp.mp.nonce = m->nonce
  };

  const struct sockaddr_in6 address = {
    .sin6_addr = GM.sta.ip6.router.sin6_addr,
    .sin6_family = AF_INET6,
    .sin6_port = htons(PCP_SERVER_PORT)
  };
  // char		buffer[INET6_ADDRSTRLEN + 1];

  gm_printf("Renew ipv6.\n");

  // gm_ntop(&last_request.address, buffer, sizeof(buffer));
  // gm_printf("Request IPv6 port mapping of router %s\n", buffer);
  // inet_ntop(AF_INET6, &address.sin6_addr, buffer, sizeof(buffer));
  // gm_printf("Client is %s\n", buffer);
 
  send_request(&p, (const struct sockaddr *)&address, ipv6_socket, sizeof(address));
}

static void
save_pcp_mapping(
  const nat_pmp_or_pcp_t * const	p,
  const gm_port_mapping_type_t		type
)
{
  const gm_port_mapping_t m = {
    .protocol = p->pcp.mp.protocol,
    .external = assign_sockaddr(
     &p->pcp.mp.external_address,
     ntohs(p->pcp.mp.external_port)),
    .internal_port = ntohs(p->pcp.mp.internal_port),
    .lifetime = ntohl(p->pcp.lifetime),
    .nonce = p->pcp.mp.nonce,
    .granted_time = current_time()
  };

  const uint8_t * const a = p->pcp.request.client_address.sin6_addr.s6_addr;
  const bool ipv4 = gm_all_zeroes(a, 10) == 0 && a[10] == 0xff && a[11] == 0xff;

  gm_port_mapping_t * *	mp;

  if ( ipv4 )
    mp = &GM.sta.ip4.port_mappings;
  else
    mp = &GM.sta.ip6.port_mappings;

  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(&m.external, buffer, sizeof(buffer));
  gm_printf("Router public mapping address: %s port: %d\n", buffer, ntohs(p->pcp.mp.external_port));

  while ( *mp ) {
    if ( memcmp(&m.nonce, &(*mp)->nonce, sizeof(m.nonce)) == 0 ) {
      gm_printf("Replaced mapping.\n");
      **mp = m;
      return;
    }
    mp = &(*mp)->next;
  }

  *mp = malloc(sizeof(**mp));
  **mp = m;

  renew_all_mappings_if_router_has_reset(p);
}

static bool
send_request(
 const nat_pmp_or_pcp_t * const	p,
 const struct sockaddr * const	address,
 const int			sock,
 size_t				address_size)
{
  // Send the packet to the gateway.
  const int sendto_status = sendto(
   sock,
   p,
   pcp_map_packet_size,
   0,
   address,
   address_size);

  if ( sendto_status < 0 ) {
    GM_FAIL_WITH_OS_ERROR("PCP sendto");
    return false;
  }
  else
    return true;
}
