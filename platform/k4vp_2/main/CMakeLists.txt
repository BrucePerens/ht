idf_component_register(
  SRCS ../user.c ../../../radio/radio.c ../../../platform/platform.c
  ../k4vp_2.c ../certificates.c
  
  PRIV_REQUIRES spi_flash
  INCLUDE_DIRS ../../../radio
  ../../../platform
  ../../../os
# Components must be listed before any other components that they depend upon.

REQUIRES
  commands
  frogfs
  generic_main
  web_handlers
)
