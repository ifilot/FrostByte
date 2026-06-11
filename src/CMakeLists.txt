cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/opt/pico-sdk")
if(NOT DEFINED PICO_BOARD)
  set(PICO_BOARD pico2 CACHE STRING "Raspberry Pi Pico board type")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(frostbyte C CXX ASM)

pico_sdk_init()

add_executable(frostbyte
  main.c
  cmds.c
  flash.c
)

target_include_directories(frostbyte PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(frostbyte
  pico_stdlib
  hardware_spi
)

# Enable USB for stdio, disable UART
pico_enable_stdio_usb(frostbyte 1)
pico_enable_stdio_uart(frostbyte 0)
pico_add_extra_outputs(frostbyte)
