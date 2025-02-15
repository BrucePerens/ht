file(GLOB DOT_C *.c)
file(GLOB DOT_CXX *.cxx)
idf_component_register(
SRCS
  ${DOT_C}
  ${DOT_CXX}
  ${CMAKE_CURRENT_BINARY_DIR}/git_version.c 
INCLUDE_DIRS
  "."
  "include"
PRIV_REQUIRES
  commands
# Components must be listed before any other components that they depend upon.
REQUIRES
  commands
  console
  vfs
  driver
  esp_netif
  esp_wifi
  esp_http_server
  esp_timer
  freertos
  generic_main
  hal
  esp-tls
  lwip
  nvs_flash
  web_handlers
)

add_custom_target(
  git-version
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/generate_git_version.sh ${CMAKE_CURRENT_BINARY_DIR}/git_version.c
)

set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/git_version.c 
    PROPERTIES GENERATED TRUE
)
add_dependencies(${COMPONENT_LIB} git-version)
