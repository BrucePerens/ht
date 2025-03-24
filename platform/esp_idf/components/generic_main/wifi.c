// FIX: If the device only got an IPV6 and no IPV4, most services would not start.

// Configure WiFi.
//
// wifi_event_sta_start() is called as an event handler after initialize()
// in main.c initializes the WiFi station and the station starts.
//
// If there is already a WiFi SSID and password (possibly blank) in
// the non-volatile parameter storage, connect to an access point with
// that SSID. If not, wait for the user to run the EspTouch app and configure
// the SSID and password.
//
// Once connected to the access point, start the web server.
//
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
/* #include "esp_eap_client.h" */
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <esp_netif_net_stack.h>
// Kludge beccause this file isn't exported, but it is necessary to get a LWIP
// struct netif * from an esp_netif_t.
#include <../lwip/esp_netif_lwip_internal.h>
#include <lwip/dhcp6.h>
#include <arpa/inet.h>
#include <lwip/sockets.h>
#include <pthread.h>
#include "generic_main.h"

enum EventBits {
  CONNECTED_BIT = 1 << 0,
  DISCONNECTED_BIT = 1 << 1,
  STATION_READY_BIT = 1 << 2,
  ESPTOUCH_DONE_BIT = 1 << 3
};

static EventGroupHandle_t wifi_events = NULL;
static esp_event_handler_instance_t handler_wifi_event_sta_connected_to_ap = NULL;
static esp_event_handler_instance_t handler_ip_event_sta_got_ip4 = NULL;
static esp_event_handler_instance_t handler_ip_event_got_ip6 = NULL;
static bool pcp_ipv6_started = false;

extern void start_webserver(void);
extern void stop_webserver();

static void wifi_connect_to_ap(const char * ssid, const char * password);
static void wifi_event_sta_start(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_event_sta_disconnected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void
gm_wifi_events_initialize(void)
{
  if (wifi_events == NULL) {
    wifi_events = xEventGroupCreate();
    xEventGroupSetBits(wifi_events, DISCONNECTED_BIT);
  }
}

void
gm_wifi_wait_until_ready(void)
{
  xEventGroupWaitBits(wifi_events, STATION_READY_BIT, true, false, portMAX_DELAY);
}

void
gm_wifi_wait_until_disconnected(void)
{
  xEventGroupWaitBits(wifi_events, DISCONNECTED_BIT, true, false, portMAX_DELAY);
}

bool
gm_wifi_is_connected(void)
{
  EventBits_t uxBits;

  uxBits = xEventGroupGetBits(wifi_events);
  return !!uxBits & CONNECTED_BIT;
}

static void after_stun(bool success, bool ipv6, struct sockaddr * address)
{
  if ( !success )
    GM_FAIL("STUN for %s failed.\n", ipv6 ? "IPv6" : "IPv4");
}

void
gm_wifi_start(void)
{
  // Initialize the TCP/IP interfaces for WiFi.
  gm_netif_t * const sta = &GM.net_interfaces[GM.next_free_net_interface++];
  sta->esp_netif = esp_netif_create_default_wifi_sta();

  // Register the event handler for WiFi station ready.
  ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &wifi_event_sta_start, NULL) );
  ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_sta_disconnected, NULL) );

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK(esp_netif_set_hostname(sta->esp_netif, GM.unique_name));
  ESP_ERROR_CHECK( esp_wifi_start() );

  // GM.net_interfaces[GM.next_free_net_interface++].esp_netif
  //  = esp_netif_create_default_wifi_ap();
}

// This is called when WiFi is ready for configuration.
// If an SSID and password are stored, attempt to connect to an access 
// point. If not, start smart configuration, and wait for the user to set
// the SSID and password from the EspTouch Android or iOS app.
//
void wifi_event_sta_start(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  char ssid[33] = { 0 };
  char password[65] = { 0 };
  size_t ssid_size = sizeof(ssid);
  size_t password_size = sizeof(password);
  // wifi_scan_config_t config = {};

  esp_err_t ssid_err = nvs_get_str(GM.nvs, "ssid", ssid, &ssid_size);
  esp_err_t password_err = nvs_get_str(GM.nvs, "wifi_password", password, &password_size);

  // config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
  // config.scan_type = WIFI_ALL_CHANNEL_SCAN;
  // config.scan_time.active.min = 120;
  // config.scan_time.active.max = 120;
  // config.scan_time.passive = 120;
  // esp_wifi_scan_start(&config, 0);

  // ssid_size includes the terminating null.
  if (ssid_err == ESP_OK && password_err == ESP_OK && ssid_size > 1)
    wifi_connect_to_ap(ssid, password);

  xEventGroupSetBits(wifi_events, STATION_READY_BIT);
}

void wifi_event_sta_disconnected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  EventBits_t uxBits;

  uxBits = xEventGroupGetBits(wifi_events);
  if ( uxBits & CONNECTED_BIT ) {
    xEventGroupClearBits(wifi_events, CONNECTED_BIT);
    xEventGroupSetBits(wifi_events, DISCONNECTED_BIT);
    gm_printf("Wifi disconnected.\n");
    gm_wifi_restart();
  }
}

static void
ipv6_router_advertisement_handler(struct sockaddr_in6 * address, uint16_t router_lifetime)
{
  if ( !gm_all_zeroes(GM.net_interfaces[GM_STA].ip6.router.s6_addr, sizeof(GM.net_interfaces[GM_STA].ip6.router.s6_addr))
   && memcmp(GM.net_interfaces[GM_STA].ip6.router.s6_addr, address->sin6_addr.s6_addr, sizeof(address->sin6_addr.s6_addr)) != 0 ) {
    GM_WARN_ONCE("Received a router advertisement from more than one IPv6 router. Ignoring all but the first.\n");
    return;
  }
  memcpy(GM.net_interfaces[GM_STA].ip6.router.s6_addr, address->sin6_addr.s6_addr, sizeof(address->sin6_addr.s6_addr));
  if ( !pcp_ipv6_started ) {
    pcp_ipv6_started = true;
    gm_pcp_start_ipv6(&GM.net_interfaces[GM_STA]);
    gm_pcp_request_mapping_ipv6(&GM.net_interfaces[GM_STA]);
  }
}

static void wifi_event_sta_connected_to_ap(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  // Start the ICMPv6 listener as early as possible, so that we get the router
  // advertisement that is solicited when the IPV6 interfaces are configured.
  gm_icmpv6_start_listener_ipv6(ipv6_router_advertisement_handler);
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_create_ip6_linklocal(GM.net_interfaces[GM_STA].esp_netif));
  dhcp6_enable_stateful(GM.net_interfaces[GM_STA].esp_netif->lwip_netif);
  dhcp6_enable_stateless(GM.net_interfaces[GM_STA].esp_netif->lwip_netif);
}

// This handler is called only when the "sta" netif gets an IPv4 address.
static void ip_event_sta_got_ip4(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
  char	buffer[INET6_ADDRSTRLEN + 1];

  // Save the IP information for other facilities, like NAT-PCP, to use.
  GM.net_interfaces[GM_STA].esp_netif = event->esp_netif;
  GM.net_interfaces[GM_STA].ip4.address.s_addr = event->ip_info.ip.addr;
  GM.net_interfaces[GM_STA].ip4.router.s_addr = event->ip_info.gw.addr;
  GM.net_interfaces[GM_STA].ip4.netmask = event->ip_info.netmask.addr;

  inet_ntop(AF_INET, &event->ip_info.ip.addr, buffer, sizeof(buffer));
  char name[6];
  esp_netif_get_netif_impl_name(event->esp_netif, name);
  gm_printf("Got IPv4: interface %s, address %s ", name, buffer);
  inet_ntop(AF_INET, &event->ip_info.gw.addr, buffer, sizeof(buffer));
  gm_printf("router %s\n", buffer);
  gm_sntp_start();
  gm_stun(false, (struct sockaddr *)&GM.net_interfaces[GM_STA].ip4.router_public_ip, after_stun);
  gm_pcp_start_ipv4(&GM.net_interfaces[GM_STA]);
  gm_pcp_request_mapping_ipv4(&GM.net_interfaces[GM_STA]);
  start_webserver();
  gm_log_server_start();
}

// This handler is called when any netif gets an IPv6 address.
static void ip_event_got_ip6(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  const unsigned int public_address_count = \
   sizeof(GM.net_interfaces[GM_STA].ip6.global) / sizeof(*GM.net_interfaces[GM_STA].ip6.global);
  char buffer[INET6_ADDRSTRLEN + 1];

  ip_event_got_ip6_t *	event = (ip_event_got_ip6_t*)event_data;
  esp_ip6_addr_type_t	ipv6_type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
  char 			netif_name[6];
  gm_netif_t *		interface;
  esp_netif_get_netif_impl_name(event->esp_netif, netif_name);
   
  inet_ntop(AF_INET6, &event->ip6_info.ip.addr, buffer, sizeof(buffer));
  gm_printf("Got IPv6: interface %s, address %s, type %s\n",
   netif_name,
   buffer,
   GM.ipv6_address_types[ipv6_type]);
  fflush(stderr);

  // FIX: Stop using interface-specific indices and just initialize the array in
  // order.
  if (strcmp(netif_name, "st1") == 0)
    interface = &GM.net_interfaces[GM_STA];
  else if (strcmp(netif_name, "ap1") == 0)
    interface = &GM.net_interfaces[GM_AP];
  else if (strcmp(netif_name, "et1") == 0)
    interface = &GM.net_interfaces[GM_ETH];
  else
    return;
  
  switch (ipv6_type) {
  case ESP_IP6_ADDR_IS_UNKNOWN:
    break;
  case ESP_IP6_ADDR_IS_GLOBAL:
    for ( unsigned int i = 0; i < public_address_count - 1; i++) {
       // If we already have this address, don't add it twice.
       if (memcmp(interface->ip6.global[i].s6_addr, event->ip6_info.ip.addr, sizeof(event->ip6_info.ip.addr)) == 0)
         break;
       // If one of the public address entries is all zeroes, copy the address into it.
       if (gm_all_zeroes(&interface->ip6.global[i].s6_addr, sizeof(&interface->ip6.global[0].s6_addr))) {
         memcpy(interface->ip6.global[i].s6_addr, event->ip6_info.ip.addr, sizeof(event->ip6_info.ip.addr));
         break;
       }
    }
    break;
  case ESP_IP6_ADDR_IS_LINK_LOCAL:
    memcpy(interface->ip6.link_local.s6_addr, event->ip6_info.ip.addr, sizeof(event->ip6_info.ip.addr));
    break;
  case ESP_IP6_ADDR_IS_SITE_LOCAL:
    memcpy(interface->ip6.site_local.s6_addr, event->ip6_info.ip.addr, sizeof(event->ip6_info.ip.addr));
    break;
  case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:
    memcpy(interface->ip6.site_unique.s6_addr, event->ip6_info.ip.addr, sizeof(event->ip6_info.ip.addr));
    break;
  case ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6:
    break;
  }
}

static void wifi_connect_to_ap(const char * ssid, const char * password)
{
  wifi_config_t cfg;

  bzero(&cfg, sizeof(cfg));
  strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
  strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));

  if (cfg.sta.password[0] == '\0')
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  else
    cfg.sta.threshold.authmode = WIFI_AUTH_WEP;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
   WIFI_EVENT,
   WIFI_EVENT_STA_CONNECTED,
   &wifi_event_sta_connected_to_ap,
   NULL,
   &handler_wifi_event_sta_connected_to_ap));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
   IP_EVENT,
   IP_EVENT_STA_GOT_IP,
   &ip_event_sta_got_ip4,
   NULL,
   &handler_ip_event_sta_got_ip4));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
   IP_EVENT,
   IP_EVENT_GOT_IP6,
   &ip_event_got_ip6,
   NULL,
   &handler_ip_event_got_ip6));

  ESP_ERROR_CHECK( esp_wifi_connect() );
}

void
gm_wifi_stop()
{
  stop_webserver();
  gm_log_server_stop();
  gm_icmpv6_stop_listener_ipv6();
  gm_pcp_stop(&GM.net_interfaces[GM_STA]);
  gm_stun_stop();
  gm_sntp_stop();
  if ( gm_wifi_is_connected() ) {
    esp_wifi_disconnect();
    gm_wifi_wait_until_disconnected();
  }
  pcp_ipv6_started = false;
}

void
gm_wifi_restart(void)
{
  char ssid[33] = { 0 };
  char password[65] = { 0 };
  size_t ssid_size = sizeof(ssid);
  size_t password_size = sizeof(password);

  gm_wifi_stop();

  esp_err_t ssid_err = nvs_get_str(GM.nvs, "ssid", ssid, &ssid_size);
  esp_err_t password_err = nvs_get_str(GM.nvs, "wifi_password", password, &password_size);

  // ssid_size includes the terminating null.
  if (ssid_err == ESP_OK && password_err == ESP_OK && ssid_size > 1 && password_size > 1) {
    wifi_connect_to_ap(ssid, password);
  }
}
