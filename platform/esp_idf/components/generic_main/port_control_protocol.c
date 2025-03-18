#include <stdint.h>
#include <sys/types.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include "generic_main.h"

typedef struct _nat_pmp_or_pcp {
  uint8_t	version;
  uint8_t	opcode; // Also contains the request/response bit.
  uint8_t	reserved;
  uint8_t	result_code;
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
          struct in6_addr client_address;
        } request;
        struct pcp_response {
          uint32_t	epoch;
          uint32_t	reserved1[3];
        } response;
      };
      union {
        struct pcp_map_peer {
          // For MAP or PEER opcode.
          uint32_t	nonce[3];
          uint8_t	protocol;
          uint8_t	reserved[3];
          uint16_t	internal_port;
          uint16_t	external_port;
          struct in6_addr external_address;
          // For PEER opcode only.
          uint16_t	remote_peer_port;
          uint16_t	reserved1;
          struct in6_addr	remote_peer_address;
        } mp;
      };
    } pcp;
  };
} nat_pmp_or_pcp_t;

static int			listener = -1;
static int			local_ipv6_socket = -1;
static struct sockaddr_in6	local_ipv6_address = {};
static uint32_t			last_received_epoch = 0;

const size_t map_packet_size = (size_t)&(((nat_pmp_or_pcp_t *)0)->pcp.mp.remote_peer_port);
const size_t announce_packet_size = (size_t)&(((nat_pmp_or_pcp_t *)0)->pcp.mp);

enum pcp_version {
  NAT_PMP = 0,
  PORT_MAPPING_PROTOCOL = 2
};

// MAP and PEER are sent from any port on the host to 5351 on the router.
// ANNOUNCE is multicast from 5351 on the router to 5350 on a host.
enum pcp_port {
  PCP_SERVER_PORT = 5351,
  PCP_CLIENT_PORT = 5350
};
const size_t	PCP_MAX_PAYLOAD = 1100;

enum pcp_opcode {
  PCP_ANNOUNCE = 0,
  PCP_MAP = 1,
  PCP_PEER = 2
};

enum nat_pmp_opcode {
  NAT_PMP_ANNOUNCE = 0,
  NAT_PMP_MAP_UDP = 1,
  NAT_PMP_MAP_TCP = 2
};

enum nat_pmp_response_code {
  NAT_PMP_SUCCESS = 0,
  NAT_PMP_UNSUPP_VERSION = 1,
  NAT_PMP_NOT_AUTHORIZED = 2,
  NAT_PMP_NETWORK_FAILURE = 3,
  NAT_PMP_OUT_OF_RESOURCES = 4,
  NAT_PMP_UNSUPPORTED_OPCODE = 5
};

// Ugh. The PCP response codes really should have included the first 5 NAT-PMP
// response codes with the same values. They just include the first 3.
enum pcp_response_code {
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
};

static void incoming_packet(int, void *, bool, bool, bool, bool);
static void renew_all_mappings();

void
request_mapping(
 nat_pmp_or_pcp_t * const	p,
 const struct sockaddr * const	address,
 const int			server,
 size_t				address_size)
{
  esp_fill_random(p->pcp.mp.nonce, sizeof(p->pcp.mp.nonce));
  p->version = PORT_MAPPING_PROTOCOL;
  p->opcode = PCP_MAP;
  p->pcp.lifetime = htonl(2 * 60);

  // Send the packet to the gateway.
  const int sendto_status = sendto(
   server,
   p,
   map_packet_size,
   0,
   address,
   address_size);

  if ( sendto_status < 0 ) {
    GM_FAIL_WITH_OS_ERROR("PCP sendto");
    return;
  }
}

void
gm_pcp_request_mapping_ipv4()
{
  nat_pmp_or_pcp_t p = {};
  struct sockaddr_in address = {};
  address.sin_addr.s_addr = GM.sta.ip4.router.sin_addr.s_addr;
  address.sin_family = AF_INET;
  address.sin_port = htons(PCP_SERVER_PORT);

  p.pcp.request.client_address.s6_addr[10] = 0xff;
  p.pcp.request.client_address.s6_addr[11] = 0xff;
  memcpy(&p.pcp.request.client_address.s6_addr[12], &GM.sta.ip4.address.sin_addr.s_addr, 4);

  // This sets the requested address to the all-zeroes IPV4 address: ::ffff:0.0.0.0 .
  // if external_address was all zeroes, it would be the all-zeroes IPV6 address.
  p.pcp.mp.external_address.s6_addr[10] = 0xff;
  p.pcp.mp.external_address.s6_addr[11] = 0xff;

  p.pcp.mp.protocol = GM_PCP_TCP;
  p.pcp.mp.internal_port = htons(443);
  p.pcp.mp.external_port = htons(7300);
  request_mapping(&p, (struct sockaddr *)&address, listener, sizeof(address));
}

 
void
gm_pcp_request_mapping_ipv6()
{
  nat_pmp_or_pcp_t p = {};
  struct sockaddr_in6 address = {};
  // char				buffer[INET6_ADDRSTRLEN + 1];

  address.sin6_family = AF_INET6;
  address.sin6_addr = GM.sta.ip6.router.sin6_addr;
  address.sin6_port = htons(PCP_SERVER_PORT);

  p.pcp.request.client_address = local_ipv6_address.sin6_addr;
  p.pcp.mp.protocol = GM_PCP_TCP;
  p.pcp.mp.internal_port = htons(443);
  p.pcp.mp.external_port = htons(7300);

  // gm_ntop(&address, buffer, sizeof(buffer));
  // gm_printf("Request IPv6 port mapping of router %s\n", buffer);
  // inet_ntop(AF_INET6, &address.sin6_addr, buffer, sizeof(buffer));
  // gm_printf("Client is %s\n", buffer);
 
  request_mapping(&p, (struct sockaddr *)&address, local_ipv6_socket, sizeof(address));
}

static void
check_pcp_epoch(const nat_pmp_or_pcp_t * const p)
{
  if ( p->pcp.response.epoch < last_received_epoch ) {
    gm_printf("The router has reset. Renewing all PCP mappings.\n");
    // The router has reset. Renew all mappings.
    last_received_epoch = p->pcp.response.epoch;
    renew_all_mappings();
  }
}

static void
decode_pcp_announce(const nat_pmp_or_pcp_t * const p, const ssize_t message_size, const struct sockaddr_storage * const address)
{
  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received PCP Announce from %s.\n", buffer);
  check_pcp_epoch(p);
}

static void
save_pcp_mapping(
  const nat_pmp_or_pcp_t * const	p,
  const esp_ip6_addr_type_t		ipv6_type,
  const gm_port_mapping_type_t		type
)
{
  gm_port_mapping_t	m = {};
  char			buffer[INET6_ADDRSTRLEN + 1];

  gettimeofday(&m.granted_time, 0);
  // We don't change the byte-order of the nonce, just send it out as it was
  // received.
  memcpy(m.nonce, p->pcp.mp.nonce, sizeof(m.nonce));
  m.protocol = p->pcp.mp.protocol;
  m.internal_port = ntohs(p->pcp.mp.internal_port);
  m.external_port = ntohs(p->pcp.mp.external_port);
  m.lifetime = ntohl(p->pcp.lifetime);
  m.external_address = p->pcp.mp.external_address;

  memset(buffer, '\0', sizeof(buffer));
  if ( ipv6_type == ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6 )
    inet_ntop(AF_INET, &p->pcp.mp.external_address.s6_addr[12], buffer, sizeof(buffer));
  else
    inet_ntop(AF_INET6, p->pcp.mp.external_address.s6_addr, buffer, sizeof(buffer));
  gm_printf("Router public mapping address: %s port: %d\n", buffer, m.external_port);

  gm_port_mapping_t * *	mp;
  if ( ipv6_type == ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6 )
   mp = &GM.sta.ip4.port_mappings;
  else
    mp = &GM.sta.ip6.port_mappings;

  while ( *mp ) {
    if ( memcmp(m.nonce, (*mp)->nonce, sizeof(m.nonce)) == 0 ) {
      gm_printf("Renewed mapping.\n");
      **mp = m;
      return;
    }
    mp = &(*mp)->next;
  }

  *mp = malloc(sizeof(**mp));
  **mp = m;

  check_pcp_epoch(p);
}

static void
decode_pcp_map(nat_pmp_or_pcp_t * p, ssize_t message_size, struct sockaddr_storage * address)
{
  // FIX: Manage re-authorization of existing mappings. Reject responses that are too
  // long after the request.
  esp_ip6_addr_t	esp_addr;
  esp_ip6_addr_type_t	ipv6_type;
  char			buffer[INET6_ADDRSTRLEN + 1];

  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received mapping from %s\n", buffer);

  inet_ntop(AF_INET6, p->pcp.mp.external_address.s6_addr, buffer, sizeof(buffer));
  // esp-idf has its own IPv6 address structure.
  memset(&esp_addr, '\0', sizeof(esp_addr));
  memcpy(esp_addr.addr, p->pcp.mp.external_address.s6_addr, sizeof(esp_addr.addr));

  // Get the address type (global, link-local, etc.) for the IPv6 address.
  ipv6_type = esp_netif_ip6_get_addr_type(&esp_addr);

#if 0
  // FIX: Match against the table of mappings.
  if ( memcmp(p->pcp.mp.nonce, last_request.packet.pcp.mp.nonce, sizeof(p->pcp.mp.nonce)) < 0 ) {
    GM_FAIL("Received nonce isn't equal to transmitted one.");
    return;
  }
#endif
  if ( p->result_code != PCP_SUCCESS ) {
    GM_FAIL("PCP received result code: %d.", p->result_code);
    return;
  }
#if 0
  // FIX: Match against the table of mappings.
  if ( p->opcode != (last_request.packet.opcode | 0x80) ) {
    GM_FAIL("Received opcode: %x, no match to last packet opcode %x.", p->opcode, last_request.packet.opcode);
    return;
  }
#endif

  if ( ipv6_type != ESP_IP6_ADDR_IS_GLOBAL
   && ipv6_type != ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6) {
    if ( memcmp(
     &p->pcp.mp.external_address,
     &GM.sta.ip6.link_local.sin6_addr,
     sizeof(GM.sta.ip6.link_local.sin6_addr)) == 0 ) {
      GM_WARN_ONCE("Warning: The router responded to a PCP map request with a useless mapping specifying the link-local address of this device, instead of the global address of the router. This is probably a MiniUPnPd bug.\n");
    }
    else {
      GM_WARN_ONCE("Warning: The router responded to a PCP map request with a useless mapping to an IPv6 %s address, %s, instead of a global address. This is probably a MiniUPnPd bug.\n", GM.ipv6_address_types[ipv6_type], buffer);
    }
  }
  else
    save_pcp_mapping(p, ipv6_type, GM_GRANTED);
}

static void
decode_pcp_peer(nat_pmp_or_pcp_t * p, ssize_t message_size, struct sockaddr_storage * address)
{
  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received PCP Peer from %s\n", buffer);
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
    port = htons(((struct sockaddr_in *)address)->sin_port);
    break;
  case AF_INET6:
    port = htons(((struct sockaddr_in6 *)address)->sin6_port);
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
    // FIX: Validate that IPV6 packet is from the router.
    break;
  }

  response = p->opcode & 0x80;
  
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
      if ( message_size < map_packet_size ) {
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
incoming_packet(int fd, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  gm_printf("Incoming packet: readable: %d, writable: %d, exception: %d, timeout: %d\n", readable, writable, exception, timeout);
  if ( timeout )
    renew_all_mappings();

  if ( !readable )
    return;

  nat_pmp_or_pcp_t		packet;
  struct sockaddr_storage	address;
  socklen_t			address_size = sizeof(address);
  ssize_t			message_size;
  // int			port;
  char			buffer[INET6_ADDRSTRLEN + 1];

  message_size = recvfrom(fd, &packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&address, &address_size);
 
  if ( message_size <= 0 ) {
    GM_FAIL_WITH_OS_ERROR("recvfrom returned %d", message_size);
    return;
  }
  memset(buffer, '\0', sizeof(buffer));
  if ( address.ss_family == AF_INET ) {
    // inet_ntop(AF_INET, &((struct sockaddr_in *)&address)->sin_addr.s_addr, buffer, sizeof(buffer));
    // port = htons(((struct sockaddr_in *)&address)->sin_port);
  }
  else {
    // inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&address)->sin6_addr, buffer, sizeof(buffer));
    // port = htons(((struct sockaddr_in6 *)&address)->sin6_port);
  }

  // gm_printf("PCP received %s packet of size %d from %s port %d.\n",
  //  data != 0 ? "multicast" : "unicast",
  //  message_size,
  //  buffer,
  //  port);

  decode_packet(&packet, message_size, &address);
}

static void
renew_ipv4(const gm_port_mapping_t * const m)
{
  gm_printf("Renew ipv4\n");
  nat_pmp_or_pcp_t	p = {};
  struct sockaddr_in	address = {};

  address.sin_addr.s_addr = GM.sta.ip4.router.sin_addr.s_addr;
  address.sin_family = AF_INET;
  address.sin_port = htons(PCP_SERVER_PORT);

  memcpy(p.pcp.mp.nonce, m->nonce, sizeof(m->nonce));
  memcpy(&p.pcp.request.client_address.s6_addr[12], &GM.sta.ip4.address.sin_addr.s_addr, 4);
  p.pcp.request.client_address.s6_addr[10] = 0xff;
  p.pcp.request.client_address.s6_addr[11] = 0xff;

  p.pcp.mp.external_address = m->external_address;
  p.pcp.mp.protocol = m->protocol;
  p.pcp.mp.internal_port = htons(m->internal_port);
  p.pcp.mp.external_port = htons(m->external_port);
  request_mapping(&p, (struct sockaddr *)&address, listener, sizeof(address));
}

static void
renew_ipv6(const gm_port_mapping_t * const m)
{
  nat_pmp_or_pcp_t	p = {};
  struct sockaddr_in6	address = {};
  // char		buffer[INET6_ADDRSTRLEN + 1];

  gm_printf("Renew ipv6.\n");
 
  address = GM.sta.ip6.router;
  address.sin6_family = AF_INET6;
  address.sin6_port = htons(PCP_SERVER_PORT);

  memcpy(p.pcp.mp.nonce, m->nonce, sizeof(m->nonce));
  p.pcp.request.client_address = local_ipv6_address.sin6_addr;
  p.pcp.mp.external_address = m->external_address;
  p.pcp.mp.protocol = m->protocol;
  p.pcp.mp.internal_port = htons(m->internal_port);
  p.pcp.mp.external_port = htons(m->external_port);

  // gm_ntop(&last_request.address, buffer, sizeof(buffer));
  // gm_printf("Request IPv6 port mapping of router %s\n", buffer);
  // inet_ntop(AF_INET6, &address.sin6_addr, buffer, sizeof(buffer));
  // gm_printf("Client is %s\n", buffer);
 
  request_mapping(&p, (struct sockaddr *)&address, local_ipv6_socket, sizeof(address));
}

static void
renew_all_mappings()
{
  gm_printf("In renew_all_mappings.\n");
  gm_port_mapping_t * m = GM.sta.ip4.port_mappings;

  if ( listener < 0 )
    return;

  while ( m ) {
    // *m is potentially changing, so save m->next before it does.
    gm_port_mapping_t * const next = m->next;
    if ( m->type == GM_GRANTED )
      renew_ipv4(m);
    m = next;
  }

  if ( local_ipv6_socket < 0 )
    return;

  m = GM.sta.ip6.port_mappings;
  while ( m ) {
    gm_port_mapping_t * const next = m->next;
    if ( m->type == GM_GRANTED )
      renew_ipv6(m);
    m = next;
  }
}

void
gm_pcp_start_ipv4()
{
  // FIX: Time-out responses and retry, eventually abandon trying.
  int			yes = 1;
  int			no = 0;

  if ( listener >= 0 )
    return;

  const struct sockaddr_in6 address = {
   .sin6_family = AF_INET6,
   // lwip defines struct in6_addr and associated things a bit differently than
   // other platforms.
   .sin6_addr = IN6ADDR_ANY_INIT,
   .sin6_port = htons(PCP_CLIENT_PORT)
  };

  listener = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

  // Reuse addresses, because other software listens for all-hosts multicast.
  // Reuse the port, because local_ipv6_socket also listens upon this port
  // with a local address instead of the IPV6 ANY address.
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  setsockopt(listener, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
  // Accept both IPV4 and IPV6 connections.
  (void) setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&no, sizeof(no)); 
  if ( bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Bind failed");
    return;
  }

  struct ip_mreq	m = {};
  m.imr_interface.s_addr = GM.sta.ip4.address.sin_addr.s_addr;
  inet_pton(AF_INET, "224.0.0.1", &m.imr_multiaddr.s_addr);
  if ( setsockopt(listener, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) < 0 ) {
    // This fails because the host is already registered to the "all-hosts" multicast
    // group. Ignore that.
    if ( errno != EADDRNOTAVAIL ) {
      GM_FAIL_WITH_OS_ERROR("Setsockopt IP_ADD_MEMBERSHIP failed");
      return;
    }
  }
  struct ipv6_mreq m6 = {};
  inet_pton(AF_INET6, "ff02::1", &m6.ipv6mr_multiaddr.s6_addr);
  m6.ipv6mr_interface = 0; // Default.
  if ( setsockopt(listener, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m6, sizeof(m6)) < 0 ) {
    // This fails because the host is already registered to the "all-hosts" multicast
    // group. Ignore that.
    if ( errno != EADDRNOTAVAIL ) {
      GM_FAIL_WITH_OS_ERROR("Setsockopt IP_ADD_MEMBERSHIP failed");
      return;
    }
  }
  gm_fd_register(listener, incoming_packet, 0, true, false, true, 1);
}

// Don't start this until a router advertisement is received.
void
gm_pcp_start_ipv6()
{
  if ( local_ipv6_socket >= 0 )
    return;

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
      local_ipv6_address.sin6_addr = addresses[i];
      bits = this_bits;
    }
  }

  if ( bits == 0 ) {
    GM_FAIL("Didn't find a local address that matches the router.");
    return;
  }

  local_ipv6_address.sin6_family = AF_INET6;
  local_ipv6_address.sin6_port = htons(PCP_CLIENT_PORT);

  local_ipv6_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if ( local_ipv6_socket < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Can't get a socket");
  }

  // Make the address reusable, because other things have bound to this address.
  // Make the port reusable, because we listen to this port with a socket bound
  // to the IPV6 ANY address, and with this socket.
  int yes = 1;
  setsockopt(local_ipv6_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  setsockopt(local_ipv6_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

  if ( bind(local_ipv6_socket, (struct sockaddr *)&local_ipv6_address, sizeof(local_ipv6_address)) < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Bind failed");
    return;
  }
  gm_fd_register(local_ipv6_socket, incoming_packet, 0, true, false, true, 0);
}

void
gm_pcp_stop(void)
{
  if ( listener >= 0 ) {
    gm_fd_unregister(listener);
    shutdown(listener, SHUT_RDWR);
    close(listener);
    listener = -1;
  }
  if ( local_ipv6_socket >= 0 ) {
    gm_fd_unregister(local_ipv6_socket);
    shutdown(local_ipv6_socket, SHUT_RDWR);
    close(local_ipv6_socket);
    local_ipv6_socket = -1;
  }
}
