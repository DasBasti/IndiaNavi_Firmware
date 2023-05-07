# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources 
    ${CMAKE_SOURCE_DIR}/src/esp32/*.*
    ${CMAKE_SOURCE_DIR}/src/icons_32/*.*
    ${CMAKE_SOURCE_DIR}/src/keys/*.*
    ${CMAKE_SOURCE_DIR}/src/screens/*.*)

idf_component_register(SRCS ${app_sources} REQUIRES lwip esp_timer vfs esp_wifi esp_event esp_netif esp_adc)

target_add_binary_data(${COMPONENT_TARGET} "keys/AmazonRootCA1-4.pem" TEXT)