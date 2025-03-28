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

typedef enum _pcp_opcode {
  PCP_ANNOUNCE = 0,
  PCP_MAP = 1,
  PCP_PEER = 2,
  PCP_OPCODE_MASK = 0x7f,
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
// Under the C language standard, implicit structure padding is not guaranteed
// to be initialized or copied, uninitialized data can leak information from
// previously-used memory. In practice, compilers generally do copy and
// initialize implicit padding.
//
// Another alternative would be not to use C structures to serialize data
// communications. I think MiniUPNP does this, and it makes it ugly, unreadable,
// and unmaintainable IMO.
#pragma pack(1)
typedef struct _pcp_packet {
  pcp_version_t version:8;
  pcp_opcode_t opcode:8; // Also contains the request/response bit.
  uint8_t reserved;
  pcp_result_code_t result_code:8;
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
} pcp_packet_t;
#pragma pack()

// MAP and PEER are sent from any port on the host to 5351 on the router.
// ANNOUNCE is multicast from 5351 on the router to 5350 on a host.
enum pcp_port {
  PCP_SERVER_PORT = 5351,
  PCP_CLIENT_PORT = 5350
};
const size_t pcp_max_payload = 1100;
const size_t pcp_map_packet_size = (size_t)&(((pcp_packet_t *)0)->mp.remote_peer_port);
const size_t pcp_announce_packet_size = (size_t)&(((pcp_packet_t *)0)->mp);

typedef gm_port_mapping_t * (*mapping_coroutine_t)(gm_port_mapping_t *, const void * const);

static bool is_ipv4_mapped_ipv6(const pcp_address_t *);
static bool send_request(const pcp_packet_t * const, const struct sockaddr * const, const int, size_t);
static gm_netif_t * interface_for_sockaddr(const struct sockaddr_storage * const, bool *);
static gm_pcp_nonce_t random_nonce();
static gm_port_mapping_t * find_mapping(const gm_pcp_nonce_t * const);
static gm_port_mapping_t * for_each_mapping(gm_port_mapping_t *, const mapping_coroutine_t, const void * const);
static gm_port_mapping_t * renew_if_granted(gm_port_mapping_t * m, const void * const);
static void decode_pcp_announce(const pcp_packet_t * const, const ssize_t, const struct sockaddr_storage * const);
static void decode_pcp_map(pcp_packet_t *, ssize_t, const struct sockaddr_storage * const);
static void decode_pcp_peer(pcp_packet_t *, ssize_t, const struct sockaddr_storage * const);
typedef gm_netif_t * (*for_each_interface_coroutine_t)(gm_netif_t *, const void * const);
static gm_netif_t * for_each_interface(const for_each_interface_coroutine_t, const void * const);
static void incoming_packet(int, void *, bool, bool, bool, bool);
static void maintain_mappings();
static void remove_pcp_mapping(gm_port_mapping_t *);
static void renew_all_mappings();
static void renew_all_mappings_if_router_has_reset(const pcp_packet_t * const);
static void renew_ipv4(const gm_port_mapping_t * const);
static void renew_ipv6(const gm_port_mapping_t * const);
static void renew_mapping(const gm_port_mapping_t * const);
static void save_pcp_mapping(const pcp_packet_t * const, gm_netif_t *, const gm_port_mapping_type_t);

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
  const bool ipv4 = is_ipv4_mapped_ipv6(a);

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

struct in6_addr
best_matching_ipv6_address(
 const gm_netif_t * const	interface,
 const struct in6_addr * const	candidate,
 uint32_t *			matching_bits)
{
  // Find the address of this interface that has the most high bits that match
  // the given  address.

  struct in6_addr result = {};

  const struct in6_addr addresses[5] = { 
    interface->ip6.link_local,
    interface->ip6.site_local,
    interface->ip6.global[0],
    interface->ip6.global[1],
    interface->ip6.global[2]
  };

  *matching_bits = 0;

  for ( size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); i++ ) {
    size_t this_bits = gm_match_bits(
     &addresses[i],
     candidate,
     sizeof(*candidate));

    if ( this_bits > *matching_bits ) {
      *matching_bits = this_bits;
      result = addresses[i];
    }
  }
  return result;
}

void
decode_packet(pcp_packet_t * p, ssize_t message_size, const struct sockaddr_storage * const address)
{
  uint16_t	port;

  gm_printf("Decode PCP packet %x\n", p->opcode);
  // This assumes all messages are PCP, we don't currently support NAT-PMP as
  // we'd only need it on really old routers.

  bool ipv4;
  gm_netif_t * interface = interface_for_sockaddr(address, &ipv4);

  if ( ipv4 ) {
    port = ntohs(((struct sockaddr_in *)address)->sin_port);
    if ( ((struct sockaddr_in *)address)->sin_addr.s_addr != interface->ip4.router.s_addr ) {
      GM_WARN_ONCE("IPV4 packet not from the router, ignored.\n");
      return;
    }
  }
  else {
    port = ntohs(((struct sockaddr_in6 *)address)->sin6_port);
    if ( memcmp(
     &((struct sockaddr_in6 *)address)->sin6_addr.s6_addr,
     &interface->ip6.router.s6_addr,
     sizeof(interface->ip6.router.s6_addr)) != 0 ) {
      GM_WARN_ONCE("IPV6 packet not from the router, ignored.\n");
      return;
    }
  }

  if ( port != PCP_SERVER_PORT ) {
    GM_WARN_ONCE("PCP: Ignoring message that isn't from the PCP port.\n");
    return;
  }

  const bool response = p->opcode & PCP_RESPONSE;
  
  switch ( p->opcode & PCP_OPCODE_MASK ) {
  case PCP_ANNOUNCE:
    decode_pcp_announce(p, message_size, address);
    break;
  case PCP_MAP:
  case PCP_PEER:
    if ( !response ) {
      GM_WARN_ONCE("PCP client received request.\n");
      return;
    }
    switch ( p->opcode & PCP_OPCODE_MASK ) {
    case PCP_MAP:
      if ( message_size < pcp_map_packet_size ) {
        GM_WARN_ONCE("PCP receive packet too small for MAP: %d\n", message_size);
        return;
      }
      decode_pcp_map(p, message_size, address);
      break;
    case PCP_PEER:
      if ( message_size < sizeof(pcp_packet_t) ) {
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
decode_pcp_announce(const pcp_packet_t * const p, const ssize_t message_size, const struct sockaddr_storage * const address)
{
  if ( p->opcode != (PCP_ANNOUNCE|PCP_SUCCESS) ) {
    GM_WARN_ONCE("Received solicitation of PCP announce.\n");
    return;
  }
  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received PCP Announce from %s.\n", buffer);
  renew_all_mappings_if_router_has_reset(p);
}

static void
decode_pcp_map(pcp_packet_t * p, ssize_t message_size, const struct sockaddr_storage * const address)
{
  char buffer[INET6_ADDRSTRLEN + 1];

  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received mapping from %s\n", buffer);
  inet_ntop(AF_INET6, &p->mp.external_address, buffer, sizeof(buffer));
  gm_printf("Router public mapping address: %s port: %d\n", buffer, ntohs(p->mp.external_port));

  // esp-idf has its own IPv6 address structure.
  esp_ip6_addr_t esp_addr = {};
  memcpy(esp_addr.addr, p->mp.external_address.sin6_addr.s6_addr, sizeof(esp_addr.addr));
  // Get the address type (global, link-local, etc.) for the IPv6 address.
  esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&esp_addr);

  gm_port_mapping_t * m = find_mapping(&p->mp.nonce);
  if ( m == 0 ) {
    GM_WARN_ONCE("PCP: Received unrequested mapping, ignoring.\n");
    return;
  }

  if ( p->result_code != PCP_SUCCESS ) {
    GM_FAIL("PCP request failed, result code: %d.", p->result_code);
    
    remove_pcp_mapping(m);
    return;
  }

  if ( ipv6_type != ESP_IP6_ADDR_IS_GLOBAL
   && ipv6_type != ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6) {
    inet_ntop(AF_INET6, p->mp.external_address.sin6_addr.s6_addr, buffer, sizeof(buffer));
    if ( memcmp(
     &p->mp.external_address.sin6_addr,
     &m->interface->ip6.link_local,
     sizeof(m->interface->ip6.link_local)) == 0 ) {
      GM_WARN_ONCE("Warning: The router responded to a PCP map request with a useless mapping specifying the link-local address of this device, instead of the global address of the router. This is probably a MiniUPnPd bug.\n");
    }
    else {
      GM_WARN_ONCE("Warning: The router responded to a PCP map request with a useless mapping to an IPv6 %s address, %s, instead of a global address. This is probably a MiniUPnPd bug.\n", GM.ipv6_address_types[ipv6_type], buffer);
    }
    remove_pcp_mapping(m);
    return;
  }
  else {
    m->type = GM_GRANTED;
    m->external = assign_sockaddr(&p->mp.external_address, ntohs(p->mp.external_port));
    m->granted_time = current_time();
    m->expiration_time.tv_sec = m->granted_time.tv_sec + ntohl(p->lifetime) - 1;
    m->expiration_time.tv_usec = m->granted_time.tv_usec;
  }
}

static void
decode_pcp_peer(pcp_packet_t * p, ssize_t message_size, const struct sockaddr_storage * const address)
{
  char buffer[INET6_ADDRSTRLEN + 1];
  gm_ntop(address, buffer, sizeof(buffer));
  gm_printf("Received PCP Peer from %s\n", buffer);
}

static gm_port_mapping_t *
find_mapping(const gm_pcp_nonce_t * const n)
{
  gm_netif_t *
  coroutine1(
   gm_netif_t * const	i,
   const void * const	d)
  {
    gm_port_mapping_t *
    coroutine2(gm_port_mapping_t * const m, const void * const d)
    {
      const gm_pcp_nonce_t * const n = (const gm_pcp_nonce_t *)d;
    
      if ( memcmp(&m->nonce, n, sizeof(*n)) == 0 )
        return m;
      else
        return 0;
    }
  
    gm_port_mapping_t * m = for_each_mapping(i->ip4.port_mappings, coroutine2, d);
    if ( m )
      return (gm_netif_t *)m;
    m = for_each_mapping(i->ip6.port_mappings, coroutine2, d);
    return (gm_netif_t *)m;
  }
  
  return (gm_port_mapping_t *)for_each_interface(coroutine1, n);
}


static gm_netif_t *
for_each_interface(const for_each_interface_coroutine_t f, const void * const d)
{
  for ( size_t n = 0; n < (sizeof(GM.net_interfaces) / sizeof(GM.net_interfaces[0])); n++ ) {
    gm_netif_t * const i = &GM.net_interfaces[n];
    if ( i ) {
      gm_netif_t * const result = f(i, d);
      if ( result )
        return result;
    }
  }

  return 0;
}

// Process each datun in a chain of port mappings.
static gm_port_mapping_t *
for_each_mapping(gm_port_mapping_t * m, const mapping_coroutine_t f, const void * d) {
  while ( m ) {
    // *m is potentially changing, so store m->next it before processing.
    gm_port_mapping_t * const next = m->next;
    gm_port_mapping_t * const result = f(m, d);
    if ( result )
      return result;
    m = next;
  }
  return 0;
}

static gm_port_mapping_t *
free_mapping(gm_port_mapping_t * m, const void * const)
{
  free(m);
  return 0;
}

void
gm_pcp_request_mapping_ipv4(gm_netif_t * interface)
{
  const pcp_packet_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .lifetime = htonl(requested_mapping_duration),

     // IPV6-mapped-IPV4 client address.
    .request.client_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .request.client_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,
    .request.client_address.ipv4_to_ipv6_mapping.s_addr = interface->ip4.address.s_addr,

    // This sets the requested address to the all-zeroes IPV6-mapped-IPV4
    // address: ::ffff:0.0.0.0 .
    // If external_address was all zeroes, it would be the all-zeroes IPV6 address.
    .mp.external_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .mp.external_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,

    .mp.protocol = GM_PCP_TCP,
    .mp.internal_port = htons(https_port),
    .mp.external_port = htons(external_https_port),
   
    .mp.nonce = random_nonce()
  };

  const struct sockaddr_in address = {
    .sin_addr.s_addr = interface->ip4.router.s_addr,
    .sin_family = AF_INET,
    .sin_port = htons(PCP_SERVER_PORT)
  };

  if ( send_request(
   &p,
   (const struct sockaddr *)&address,
   ipv4_socket,
   sizeof(address)) )
    save_pcp_mapping(&p, interface, GM_REQUEST);
}

void
gm_pcp_request_mapping_ipv6(gm_netif_t * interface)
{
  const pcp_packet_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .lifetime = htonl(requested_mapping_duration),

    .request.client_address.sin6_addr = my_ipv6_address.sin6_addr,
    .mp.protocol = GM_PCP_TCP,
    .mp.internal_port = htons(https_port),
    .mp.external_port = htons(external_https_port),
    .mp.nonce = random_nonce()
  };

  const struct sockaddr_in6 address = {
    .sin6_family = AF_INET6,
    .sin6_addr = interface->ip6.router,
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
    save_pcp_mapping(&p, interface, GM_REQUEST);
}

void
gm_pcp_start_ipv4(gm_netif_t * interface)
{
  int	yes = 1;
  char	name[6];

  ESP_ERROR_CHECK(esp_netif_get_netif_impl_name(interface->esp_netif, name));
  gm_printf(
   "PCP: Starting netif with index %d, name %s\n",
   esp_netif_get_netif_impl_index(interface->esp_netif),
   name);

  if ( ipv4_socket >= 0 ) {
    gm_printf("Socket already open.\n");
    return;
  }

  ipv4_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // Reuse addresses, because other software listens for all-hosts multicast.
  setsockopt(ipv4_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  // setsockopt(ipv4_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

  const struct sockaddr_in address = {
   .sin_family = AF_INET,
   .sin_addr.s_addr = INADDR_ANY,
   .sin_port = htons(PCP_CLIENT_PORT)
  };

  if ( bind(ipv4_socket, (struct sockaddr *)&address, sizeof(address)) < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Bind failed");
    return;
  }

  const struct ip_mreq m = {
   .imr_interface.s_addr = interface->ip4.address.s_addr,
   .imr_multiaddr.s_addr = htonl((224 << 24) + 1) // 224.0.0.1
  };
  if ( setsockopt(ipv4_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) < 0 ) {
    // This fails because the host is already registered to the "all-hosts" multicast
    // group. Ignore that.
    if ( errno != EADDRNOTAVAIL ) {
      GM_FAIL_WITH_OS_ERROR("Setsockopt IP_ADD_MEMBERSHIP failed");
      return;
    }
  }
  gm_fd_register(ipv4_socket, incoming_packet, 0, true, false, true, 1);
  gm_printf("Done with IPV4 socket.\n");
}

// Don't start this until a router advertisement is received.
void
gm_pcp_start_ipv6(gm_netif_t * interface)
{
  struct in6_addr result = {};
  uint32_t matching_bits = 0;

  if ( ipv6_socket >= 0 )
    return;

  // Find the local address that has the most high bits that match the router's
  // address. Use that address to communicate with the router.
  result = best_matching_ipv6_address(
   interface,
   &interface->ip6.router,
   &matching_bits);


  if ( matching_bits == 0 ) {
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
  // setsockopt(ipv6_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

  my_ipv6_address.sin6_addr = result;
  if ( bind(ipv6_socket, (struct sockaddr *)&my_ipv6_address, sizeof(my_ipv6_address)) < 0 ) {
    GM_FAIL_WITH_OS_ERROR("Bind failed");
    return;
  }
  const struct ipv6_mreq m6 = {
    .ipv6mr_interface = 0, // Default.
    .ipv6mr_multiaddr.s6_addr = {0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,1}
  };
  if ( setsockopt(ipv6_socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m6, sizeof(m6)) < 0 ) {
    // This fails because the host is already registered to the "all-hosts" multicast
    // group. Ignore that.
    if ( errno != EADDRNOTAVAIL ) {
      GM_FAIL_WITH_OS_ERROR("Setsockopt IP_ADD_MEMBERSHIP failed");
      return;
    }
  }
  gm_fd_register(ipv6_socket, incoming_packet, 0, true, false, true, 0);
}

void
gm_pcp_stop(gm_netif_t * interface)
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
  gm_netif_t *
  coroutine(gm_netif_t * const interface, const void * const data)
  {
    for_each_mapping(interface->ip4.port_mappings, free_mapping, 0);
    interface->ip4.port_mappings = 0;
    for_each_mapping(interface->ip6.port_mappings, free_mapping, 0);
    interface->ip6.port_mappings = 0;
    return 0;
  }
  
  for_each_interface(coroutine, 0);
}

static void
incoming_packet(int fd, void * data, bool readable, bool writable, bool exception, bool timeout)
{
  gm_printf("Incoming packet: readable: %d, writable: %d, exception: %d, timeout: %d\n", readable, writable, exception, timeout);

  if ( readable ) {
    pcp_packet_t		packet;
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

gm_netif_t *
interface_for_sockaddr(const struct sockaddr_storage * const s, bool * ipv4)
{
  *ipv4 = s->ss_family == AF_INET;

  gm_netif_t *	interface = 0;
  uint32_t	best_match_so_far = 0;
  const size_t	number_of_interfaces =
   sizeof(GM.net_interfaces) / sizeof(GM.net_interfaces[0]);

  for ( size_t i = 0; i < number_of_interfaces; i++ ) {
    if ( *ipv4 ) {
      const uint32_t matching_bits = gm_match_bits(
       &GM.net_interfaces[i].ip4.address,
       &((struct sockaddr_in *)s)->sin_addr.s_addr,
       sizeof(GM.net_interfaces[0].ip4.address));

       if ( matching_bits > best_match_so_far ) {
         best_match_so_far = matching_bits;
         interface = &GM.net_interfaces[i];
       }
    }
    else {
       uint32_t	matching_bits;

       (void) best_matching_ipv6_address(
        &GM.net_interfaces[i],
        &((struct sockaddr_in6 *)s)->sin6_addr,
        &matching_bits);

       if ( matching_bits > best_match_so_far ) {
         best_match_so_far = matching_bits;
         interface = &GM.net_interfaces[i];
       }
    }
  }
  return interface;
}

static bool
is_ipv4_mapped_ipv6(const pcp_address_t * a)
{
  return gm_all_zeroes(
   a->ipv4_to_ipv6_mapping.all_zeroes,
   sizeof(a->ipv4_to_ipv6_mapping.all_zeroes))
  && a->ipv4_to_ipv6_mapping.all_ones[0] == 0xff
  && a->ipv4_to_ipv6_mapping.all_ones[1] == 0xff;
}

static void
maintain_mappings()
{
  gm_port_mapping_t *
  coroutine(gm_port_mapping_t * m, const void *)
  {
    const struct timeval now = current_time();
  
    if ( m->type == GM_GRANTED ) {
      if ( timercmp(&m->expiration_time, &now, >) ) {
        m->type = GM_REQUEST;
        renew_mapping(m);
      }
      else {
        struct timeval remaining;
        timersub(&m->expiration_time, &now, &remaining);
        if ( remaining.tv_sec < m->lifetime / 3 )
          renew_mapping(m);
      }
    }
    else {
      // This is a requested mapping that hasn't been confirmed by the server.
      renew_mapping(m);
    }
    return 0;
  }
  
  gm_printf("In maintain_mappings.\n");
  if ( ipv4_socket >= 0 )
    for_each_mapping(GM.net_interfaces[GM_STA].ip4.port_mappings, coroutine, 0);

  if ( ipv6_socket >= 0 )
    for_each_mapping(GM.net_interfaces[GM_STA].ip6.port_mappings, coroutine, 0);
}

// Provide random nonce data in assignable form, so that I can make
// structures const.
static gm_pcp_nonce_t
random_nonce() {
  gm_pcp_nonce_t n;
  esp_fill_random(&n, sizeof(n));
  return n;
}

// I could do this using pointers to pointers and a custom loop.
static void
remove_pcp_mapping(gm_port_mapping_t * m)
{
  if ( m == GM.net_interfaces[GM_STA].ip4.port_mappings ) {
    GM.net_interfaces[GM_STA].ip4.port_mappings = GM.net_interfaces[GM_STA].ip4.port_mappings->next;
    return;
  }
  if ( m == GM.net_interfaces[GM_STA].ip6.port_mappings ) {
    GM.net_interfaces[GM_STA].ip6.port_mappings = GM.net_interfaces[GM_STA].ip6.port_mappings->next;
    return;
  }

  gm_port_mapping_t *
  coroutine(gm_port_mapping_t * m, const void * n)
  {
    while ( m ) {
      if ( m->next == n ) {
        m->next = m->next->next;
        free(m);
        return (gm_port_mapping_t *)1;
      }
    }
    return 0;
  }
  
  if ( for_each_mapping(GM.net_interfaces[GM_STA].ip4.port_mappings, coroutine, m) )
    return;
  if ( for_each_mapping(GM.net_interfaces[GM_STA].ip6.port_mappings, coroutine, m) )
    return;
}

static void
renew_all_mappings()
{
  gm_printf("In renew_all_mappings.\n");

  gm_netif_t *
  coroutine(gm_netif_t * interface, const void * const)
  {
    if ( ipv4_socket >= 0 )
      for_each_mapping(GM.net_interfaces[GM_STA].ip4.port_mappings, renew_if_granted, 0);
  
    if ( ipv6_socket >= 0 )
  	  for_each_mapping(GM.net_interfaces[GM_STA].ip6.port_mappings, renew_if_granted, 0);
    return 0;
  }
  
  for_each_interface(coroutine, 0);
}

static void
renew_all_mappings_if_router_has_reset(const pcp_packet_t * const p)
{
  if ( p->response.epoch < last_received_epoch ) {
    gm_printf("The router has reset. Renewing all PCP mappings.\n");
    last_received_epoch = p->response.epoch;
    renew_all_mappings();
  }
}

static gm_port_mapping_t *
renew_if_granted(gm_port_mapping_t * m, const void * const)
{
  if ( m->type == GM_GRANTED )
    renew_mapping(m);
  return 0;
}

static void
renew_ipv4(const gm_port_mapping_t * const m)
{
  gm_printf("Renew ipv4\n");
  const struct sockaddr_in * const s = (struct sockaddr_in *)&m->external;

  const pcp_packet_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .lifetime = htonl(m->lifetime),

    .request.client_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .request.client_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,
  
    // PCP uses IPV4-mapped-IPV6 addresses.
    .mp.external_address.ipv4_to_ipv6_mapping.all_ones[0] = 0xff,
    .mp.external_address.ipv4_to_ipv6_mapping.all_ones[1] = 0xff,
    .mp.external_address.ipv4_to_ipv6_mapping.s_addr = s->sin_addr.s_addr,
    .mp.external_port = htons(s->sin_port),
    .mp.protocol = m->protocol,
    .mp.internal_port = htons(((struct sockaddr_in *)&m->internal)->sin_port),
    .mp.nonce = m->nonce
  };

  const struct sockaddr_in address = {
    .sin_addr.s_addr = m->interface->ip4.router.s_addr,
    .sin_family = AF_INET,
    .sin_port = htons(PCP_SERVER_PORT)
  };

  send_request(&p, (struct sockaddr *)&address, ipv4_socket, sizeof(address));
}

static void
renew_ipv6(const gm_port_mapping_t * const m)
{
  struct sockaddr_in6 * s = (struct sockaddr_in6 *)&m->external;
  const pcp_packet_t p = {
    .version = PORT_MAPPING_PROTOCOL,
    .opcode = PCP_MAP,
    .lifetime = htonl(m->lifetime),

    .request.client_address.sin6_addr = my_ipv6_address.sin6_addr,
    .mp.external_address.sin6_addr = s->sin6_addr,
    .mp.external_port = htons(s->sin6_port),
    .mp.protocol = m->protocol,
    .mp.internal_port = htons(((struct sockaddr_in6 *)&m->internal)->sin6_port),
    .mp.nonce = m->nonce
  };

  const struct sockaddr_in6 address = {
    .sin6_addr = m->interface->ip6.router,
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
renew_mapping(const gm_port_mapping_t * const m)
{
  if ( m->external.ss_family == AF_INET )
    renew_ipv4(m);
  else
    renew_ipv6(m);
}
  
static void
save_pcp_mapping(
  const pcp_packet_t * const	p,
  gm_netif_t *				interface,
  const gm_port_mapping_type_t		type
)
{
  const gm_port_mapping_t m = {
    .protocol = p->mp.protocol,
    .internal = assign_sockaddr(
     &p->request.client_address,
     ntohs(p->mp.internal_port)),
    .external = assign_sockaddr(
     &p->mp.external_address,
     ntohs(p->mp.external_port)),
    .lifetime = ntohl(p->lifetime),
    .nonce = p->mp.nonce,
    .granted_time = current_time()
  };

  const bool ipv4 = is_ipv4_mapped_ipv6(&p->request.client_address);

  gm_port_mapping_t * *	mp;

  if ( ipv4 )
    mp = &interface->ip4.port_mappings;
  else
    mp = &interface->ip6.port_mappings;

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
 const pcp_packet_t * const	p,
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
