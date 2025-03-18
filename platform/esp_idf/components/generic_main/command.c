#include <stdlib.h>
#include <string.h>
#include "generic_main.h"
#include <esp_console.h>
#include <driver/uart.h>
#include <driver/usb_serial_jtag.h>
#include "linenoise/linenoise.h"

static GM_Array * array = 0;

// This is called from CONSTRUCTOR functions, before main().
void
gm_command_register(const esp_console_cmd_t * command)
{
  if ( !array ) {
    array = gm_array_create();
  }
  gm_array_add(array, (const void *)command);
}

static int
compare(const void * _a, const void * _b)
{
  const esp_console_cmd_t * a = *(const esp_console_cmd_t * *)_a;
  const esp_console_cmd_t * b = *(const esp_console_cmd_t * *)_b;
  int result = strcmp(a->command, b->command);
  return result;
}

void
gm_command_add_registered_to_console(void)
{
  size_t size;

  if (!array || (size = gm_array_size(array)) == 0) {
    GM_FAIL("No commands are registered.");
    return;
  }
  
  // Alphabetically sort the commands.
  qsort(gm_array_data(array), gm_array_size(array), sizeof(void *), compare);

  for ( size_t i = 0; i < size; i++ ) {
    const esp_console_cmd_t * c = (const esp_console_cmd_t *)gm_array_get(array, i);

    ESP_ERROR_CHECK(esp_console_cmd_register(c));
  }
  gm_array_destroy(array);
}

void
gm_command_interpreter_start(void)
{
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.task_stack_size = 5 * 1024;
  repl_config.prompt = ">";
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &GM.repl));
#else
  esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &GM.repl));
#endif

  // Don't start the REPL thread if there isn't a terminal connected.
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  if ( !usb_serial_jtag_is_connected() )
    return;
#else
  // ANSI Terminals will report the cursor position in response to this.
  char device_status_report[] = "\x1b[6n";
  uart_flush(0);
  write(1, device_status_report, sizeof(device_status_report) - 1);
  char	buf[20];
  if ( uart_read_bytes(0, buf, sizeof(buf), 10) <= 0 )
    return;
#endif

  // Configure the console command system.
  gm_command_add_registered_to_console();
  ESP_ERROR_CHECK(esp_console_start_repl(GM.repl));
}
