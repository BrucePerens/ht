#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_console.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <esp_netif_net_stack.h>
#include <esp_event.h>
#include <esp_https_server.h>
#include <netinet/in.h>
#include <lwip/dhcp6.h>
#include <../lwip/esp_netif_lwip_internal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <esp_debug_helpers.h>


#define CONSTRUCTOR static void __attribute__ ((constructor))

#define COUNTOF(a) (sizeof((a)) / sizeof(*(a)))
/* Self-allocating vsnprintf(). This relies on GCC-specific extensions to C */
#define GM_VSPRINTF(pattern) \
( \
  { \
    char b##__COUNTER__[128]; \
    va_list a##__COUNTER__; \
    va_start(a##__COUNTER__, (pattern)); \
    vsnprintf(b##__COUNTER__, sizeof(b##__COUNTER__), (pattern), a##__COUNTER__); \
    va_end(a##__COUNTER__); \
    b##__COUNTER__; \
  } \
)

extern void gm_fail(const char *, const char *, int, const char *, ...);
#define GM_FAIL(args...) gm_fail(__PRETTY_FUNCTION__, __FILE__, __LINE__, args)
extern void gm_fail_with_os_error(const char *, const char *, int, const char *, ...);
#define GM_FAIL_WITH_OS_ERROR(args...) gm_fail_with_os_error(__PRETTY_FUNCTION__, __FILE__, __LINE__, args)
#define GM_WARN_ONCE(args...) { static bool i_told_you_once = false; if ( !i_told_you_once ) { gm_printf(args); i_told_you_once = true; } }

ESP_EVENT_DECLARE_BASE(GM_EVENT);

typedef enum _gm_nonvolatile_result {
  GM_ERROR = -2,
  GM_NOT_IN_PARAMETER_TABLE = -1,
  GM_NORMAL = 0,
  GM_SECRET = 1,
  GM_NOT_SET = 2
} gm_nonvolatile_result_t;

typedef enum gm_nonvolatile_type {
  END = 0,
  STRING,
  INT,
  FLOAT,
  URL,
  DOMAIN
} gm_nonvolatile_type_t;

typedef enum _gm_web_method {
  GET = 0,
  PUT = 1,
  POST = 2
} gm_web_method;

typedef enum _gm_run_speed {
  GM_SLOW,
  GM_MEDIUM,
  GM_FAST
} gm_run_speed_t;

typedef enum _gm_event_id {
  GM_RUN
} gm_event_id_t;

typedef void (*gm_fd_handler_t)(int fd, void * data, bool readable, bool writable, bool exception, bool timeout);
typedef void (*gm_run_t)(void *);
typedef void (*gm_stun_after_t)(bool success, bool ipv6, struct sockaddr * address);
typedef void (*gm_ipv6_router_advertisement_after_t)(struct sockaddr_in6 * address, uint16_t lifetime);

typedef struct _gm_run_data {
  gm_run_t	procedure;
  void *	data;
} gm_run_data_t;

typedef enum _gm_port_mapping_type {
  GM_REQUEST,
  GM_GRANTED
} gm_port_mapping_type_t;

typedef enum _gm_pcp_protocol {
  GM_PCP_TCP = 6,
  GM_PCP_UDP = 17
} gm_pcp_protocol_t;

// This is an assignable version of the PCP nonce data, rather than a raw array.
// It must be the same size as the underlying data.
#pragma pack(1)
typedef struct _gm_pcp_nonce {
  uint32_t data[3];
} gm_pcp_nonce_t;
#pragma pack()

struct _gm_netif;
typedef struct _gm_netif gm_netif_t;

// This is arranged in the hope of reducing unnecessary padding.
// Note the enums restricted in size as bit-fields.
typedef struct _gm_port_mapping { 
  uint32_t			lifetime;
  struct _gm_port_mapping *	next;
  gm_netif_t *			interface;
  struct timeval		granted_time;
  struct timeval		expiration_time;
  struct sockaddr_storage	internal;
  struct sockaddr_storage	external;
  gm_pcp_nonce_t		nonce;
  uint8_t			request_count;
  gm_pcp_protocol_t		protocol:6;
  gm_port_mapping_type_t	type:2;
} gm_port_mapping_t;

struct _gm_netif {
  esp_netif_t *		esp_netif;
  // lwip_netif is esp_netif->lwip_netif
  struct gm_netif_ip4 {
    struct in_addr	address;
    struct in_addr	router;
    uint32_t		netmask;
    struct in_addr	router_public_ip;
    int			nat;	// 1 for NAT, 2 for double-nat.
    gm_port_mapping_t *	port_mappings;
  } ip4;
  struct gm_netif_ip6 {
    struct in6_addr	link_local;
    struct in6_addr	site_local;
    struct in6_addr	site_unique;
    struct in6_addr	global[3];
    struct in6_addr	router;
    struct in6_addr	router_public_ip;
    gm_port_mapping_t *	port_mappings;
    bool pat66; // True if there is prefix-address-translation. Ugh.
    bool nat6;  // True if there is NAT6 that is not PAT66. Double-ugh.
  } ip6;
};

// Don't put the user's language in here, the browser should send it in the
// Accept-Language header.
// Put email addresses in nvs separately so that they can be stored as strings
// of variable length. The longest legal email address is 254 characters (not 320). 
// WARNING: The longest fields here may not have a 0 string terminator.
// Code must process them with knowledge of that.
typedef struct _gm_user_data {
  // 6-letter call, "/", three-letter location, "-", two digits.
  char		callsign[13];
  // AES processes data with a multiple of 16 in size.
  char	password[32];
  // ITU country code, for operating privileges.
  char  country[3];
  // License class for operating privileges. Assuming longest is "technician+";
  char	license_class[11];
  // 8 bits of flags available before we change the structure size.
  uint8_t	admin:1;
  uint8_t	transmit:1;
  uint8_t	banned:1;
} gm_user_data_t;

typedef struct _gm_session_context {
  cJSON *		json;
  char *		user_name;
  gm_user_data_t	user_data;
} gm_session_context_t;

enum _gm_interface_index {
  GM_AP,
  GM_STA,
  GM_ETH
};

typedef struct _generic_main {
  nvs_handle_t		nvs;
  gm_netif_t		net_interfaces[3]; // ap, sta, eth
  esp_console_repl_t *	repl;
  pthread_mutex_t 	console_print_mutex;
  int64_t		time_last_synchronized;
  uint8_t		factory_mac_address[6];
  const char *		application_name;
  char			unique_name[64];
  esp_event_loop_handle_t medium_event_loop;
  esp_event_loop_handle_t slow_event_loop;
  const char * const	build_version;
  const char * const	build_number;
  const char * const	nvs_index;
  const char * const	ipv6_address_types[6];
  int			log_fd;
  FILE *		log_file_pointer;
  esp_aes_context	aes_cookie_context;
  // We use the same AES initialization vector for cookie encoding all of the time,
  // to avoid sending the IV as a second cookie.
  // So the cookie plaintext _must_ contain randomness.
  uint8_t		aes_cookie_iv[16];
  uint8_t		hmac_key[64];
  // Set to the error if there is an indication that FLASH is failing.
  esp_err_t		flash_failure ;
} generic_main_t;

typedef struct _gm_param_t {
  const char *	name;
  const char *	value;
} gm_param_t;

typedef struct _gm_uri {
  char		path[512];
  gm_param_t	params[10];
} gm_uri;

typedef struct gm_web_handler {
  const char *	name;
  int		(*handler)(httpd_req_t * request, const gm_uri * uri);
  struct gm_web_handler * next;
} gm_web_handler_t;

struct _GM_Array;

typedef struct _GM_Array GM_Array;
typedef void (*gm_nonvolatile_list_coroutine_t)(const char *, const char *, const char *, gm_nonvolatile_result_t);
typedef int (*gm_pattern_coroutine_t)(const char * name, char * result, size_t result_size);
typedef void (*gm_web_get_coroutine_t)(const char * data, size_t size);

extern generic_main_t		GM;

extern bool			gm_all_zeroes(const void *, size_t);
extern const void *		gm_array_add(GM_Array * array, const void * data);
extern GM_Array *		gm_array_create(void);
extern const void * *		gm_array_data(GM_Array * array);
extern void			gm_array_destroy(GM_Array * array);
extern const void *		gm_array_get(GM_Array * array, size_t index);
extern size_t			gm_array_size(GM_Array * array);

extern size_t			gm_choose_one(size_t number_of_entries);
extern void			gm_command_add_registered_to_console(void);
extern void			gm_command_interpreter_start(void);
extern void			gm_command_register(const esp_console_cmd_t * command);
extern esp_err_t		gm_flash_failure(const char *, esp_err_t err);
extern int			gm_ddns(void);

extern void			gm_event_server(void);

extern void			gm_get_handlers(httpd_handle_t server);
extern esp_err_t		gm_get_user_data(const char * name, gm_user_data_t * data);
extern cJSON *			gm_read_cookie(httpd_req_t * req);
extern size_t			gm_match_bits(const void * const restrict av, const void * const restrict bv, size_t size);
extern void			gm_sntp_start();
extern void			gm_sntp_stop();
extern esp_err_t		gm_start_redirect_to_https();
extern void			gm_stop_redirect_to_https();
extern void			gm_run(gm_run_t function, void * data, gm_run_speed_t speed);
extern void			gm_fd_register(int fd, gm_fd_handler_t handler, void * data, bool readable, bool writable, bool exception, uint32_t seconds);
extern void			gm_fd_unregister(int fd);

extern void			gm_icmpv6_start_listener_ipv6(gm_ipv6_router_advertisement_after_t after);
extern void			gm_icmpv6_stop_listener_ipv6(void);

extern void			gm_improv_wifi(int fd);

extern void			gm_log_server_start(void);
extern void			gm_log_server_stop(void);

extern gm_nonvolatile_result_t	gm_nonvolatile_erase(const char * name);
extern gm_nonvolatile_result_t	gm_nonvolatile_get(const char * name, char * buffer, size_t size);
extern void			gm_nonvolatile_list(gm_nonvolatile_list_coroutine_t coroutine);
extern gm_nonvolatile_result_t	gm_nonvolatile_set(const char * name, const char * value);

extern void			gm_ntop(const struct sockaddr_storage * const s, char * const buffer, const size_t size);
extern const char *		gm_param(const gm_param_t * p, int count, const char * name);
extern int			gm_param_parse(const char * s, gm_param_t * p, int count);
extern int			gm_pattern_string(const char * string, gm_pattern_coroutine_t coroutine, char * buffer, size_t buffer_size);
extern void			gm_pcp_request_mapping_ipv4(gm_netif_t *);
extern void			gm_pcp_request_mapping_ipv6(gm_netif_t *);
extern void			gm_pcp_start_ipv4(gm_netif_t *);
extern void			gm_pcp_start_ipv6(gm_netif_t *);
extern void			gm_pcp_stop(gm_netif_t *);
extern int			gm_printf(const char * format, ...);
extern int			gm_public_ipv4(char * data, size_t size);

extern void			gm_self_signed_ssl_certificates(struct httpd_ssl_config * c);
extern void			gm_session(httpd_req_t * req);
extern int			gm_stun(bool ipv6, struct sockaddr * address, gm_stun_after_t after);
extern void			gm_stun_stop();

extern void			gm_select_task(void);
extern void			gm_select_wakeup(void);

extern void			gm_timer_to_human(int64_t, char *, size_t);

extern void			gm_uart_initialize(void);
extern void			gm_user_initialize_early(void);
extern void			gm_user_initialize_late(void);
extern int			gm_uri_decode(const char * uri, char * buffer, size_t size);
extern int			gm_uri_parse(const char * uri, gm_uri * u) ;

extern int			gm_vprintf(const char * format, va_list args);

extern void			gm_web_finish();
extern int			gm_web_get(const char *url, char *data, size_t size);
extern int			gm_web_get_with_coroutine(const char *url, gm_web_get_coroutine_t coroutine);
extern void			gm_web_handler_install(httpd_handle_t server);
extern void			gm_web_handler_register(gm_web_handler_t * handler, gm_web_method method);
extern int			gm_web_handler_run(httpd_req_t * req, const gm_uri * uri, gm_web_method method);
extern void			gm_web_send_to_client (const char *d, size_t size);
extern void			gm_web_set_request(void * context);

extern bool			gm_wifi_is_connected(void);
extern void			gm_wifi_events_initialize(void);
extern void			gm_wifi_restart(void);
extern void			gm_wifi_start(void);
extern void			gm_wifi_stop(void);
extern void			gm_wifi_wait_until_disconnected(void);
extern void			gm_wifi_wait_until_ready(void);

void				gm_write_cookie(httpd_req_t * const req, cJSON * const json);
