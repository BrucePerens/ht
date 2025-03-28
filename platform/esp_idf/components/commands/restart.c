#include <stdio.h>
#include <stdlib.h>
#include <esp_console.h>
#include <esp_system.h>
#include "generic_main.h"

static int restart(int argc, char * * argv)
{
  // Need to stop wifi first, or static variables will be left in the wrong state.
  gm_wifi_stop();
  esp_restart();
}

CONSTRUCTOR install(void)
{
  static const esp_console_cmd_t command = {
    .command = "restart",
    .help = "Restart this program.",
    .hint = NULL,
    .func = &restart,
    .argtable = NULL
  };

  gm_command_register(&command);
}
