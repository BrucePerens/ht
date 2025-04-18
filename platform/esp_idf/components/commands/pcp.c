#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include "generic_main.h"

static struct {
    struct arg_lit * ipv6;
    struct arg_end * end;
} args;

static int run(int argc, char * * argv)
{
  int nerrors = arg_parse(argc, argv, (void **) &args);
  if (nerrors) {
    arg_print_errors(stderr, args.end, argv[0]);
      return 1;
  }
  printf("\n"); 
  if ( args.ipv6->count > 0 )
    gm_pcp_request_mapping_ipv6(&GM.net_interfaces[GM_STA]);
  else
    gm_pcp_request_mapping_ipv4(&GM.net_interfaces[GM_STA]);

  return 0;
}

CONSTRUCTOR install(void)
{
  args.ipv6 =  arg_lit0("6", NULL, "Use IPv6 (default IPv4)");
  args.end = arg_end(10);
  static const esp_console_cmd_t command = {
    .command = "pcp",
    .help = "Run the port control protocol to get a firewall pinhole.",
    .hint = NULL,
    .func = &run,
    .argtable = &args
  };

  gm_command_register(&command);
}
