# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esppdp)
spiffs_create_partition_image(storage spiffs/ FLASH_IN_PROJECT)
set_target_properties(${COMPONENT_LIB} PROPERTIES COMPILE_FLAGS -Wformat=n)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)

