cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(IndiaNavi_Firmware)

# With -O2 GCC 14 sees an array access out of bounds in NimBLE's bond store that never runs
# with CONFIG_BT_NIMBLE_MAX_BONDS=1 (the loop runs 0 times). Keep the warning, but not as error.
idf_component_get_property(bt_lib bt COMPONENT_LIB)
target_compile_options(${bt_lib} PRIVATE -Wno-error=array-bounds)
