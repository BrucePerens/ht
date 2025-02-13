idf_component_register(
  SRCS ../k6vp_main.c ${CMAKE_CURRENT_SOURCE_DIR}/../../../radio/radio.c ${CMAKE_CURRENT_SOURCE_DIR}/../../../platform/platform.c ${CMAKE_CURRENT_SOURCE_DIR}/../../../platform/k6vp_2/k6vp_2.c
  PRIV_REQUIRES spi_flash
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../../../radio ${CMAKE_CURRENT_SOURCE_DIR}/../../../platform ${CMAKE_CURRENT_SOURCE_DIR}/../../../os)

