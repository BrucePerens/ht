idf_component_register(
  SRCS ../k4vp_main.c ../user.c ${CMAKE_CURRENT_SOURCE_DIR}/../../../radio/radio.c ${CMAKE_CURRENT_SOURCE_DIR}/../../../platform/platform.c ${CMAKE_CURRENT_SOURCE_DIR}/../../../platform/k4vp_2/k4vp_2.c
  PRIV_REQUIRES spi_flash
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../../../radio ${CMAKE_CURRENT_SOURCE_DIR}/../../../platform ${CMAKE_CURRENT_SOURCE_DIR}/../../../os
# Components must be listed before any other components that they depend upon.
# Components must be listed before any other components that they depend upon.
REQUIRES
  commands
  console
  driver
  esp_netif
  esp_http_server
  esp_timer
  freertos
  generic_main
  hal
  lwip
  nvs_flash
  web_handlers
)
