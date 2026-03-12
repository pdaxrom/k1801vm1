cmake_minimum_required(VERSION 3.13)

set(ILI9486L_LCD_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(ILI9486L_LCD_IS_TOP_LEVEL ON)
endif()

if(NOT COMMAND pico_sdk_init)
  if(ILI9486L_LCD_IS_TOP_LEVEL)
    if(EXISTS $ENV{HOME}/cross-env.cmake)
      message(STATUS "Custom cross environment detected")
      include($ENV{HOME}/cross-env.cmake)
    else()
      message(STATUS "Custom cross environment is not detected")
    endif()
  endif()

  if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
  endif()

  if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Configure it in $ENV{HOME}/cross-env.cmake, export it in the environment, or initialize the Pico SDK in the parent project before add_subdirectory().")
  endif()

  include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(ili9486l_lcd C CXX ASM)

option(ILI9486L_LCD_BUILD_DEMO "Build the demo application from demo/" ${ILI9486L_LCD_IS_TOP_LEVEL})
option(ILI9486L_LCD_ENABLE_DEMO_STDIO_USB "Enable USB stdio for the demo application" ON)
option(ILI9486L_LCD_ENABLE_DEMO_STDIO_UART "Enable UART stdio for the demo application" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Pico SDK for RP2350 uses GNU C constructs such as `asm` in C sources.
# Keep GNU extensions enabled so both RP2040 and Pico 2 builds work.
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET pico_stdlib)
  pico_sdk_init()
endif()

set(ILI9486L_LCD_PUBLIC_HEADERS
  include/ili9486l.h
  include/ili9486l_jpeg.h
  include/vt100_terminal.h
)

set(ILI9486L_LCD_LIBRARY_SOURCES
  src/font5x7.c
  src/font5x7_row_masks.c
  src/ili9486l.c
  src/ili9486l_jpeg.c
  src/tjpgd/tjpgd.c
  src/vt100_terminal.c
  src/vt100_terminal_console.c
)

add_library(ili9486l_lcd STATIC
  ${ILI9486L_LCD_PUBLIC_HEADERS}
  ${ILI9486L_LCD_LIBRARY_SOURCES}
)

add_library(ili9486l::lcd ALIAS ili9486l_lcd)

target_include_directories(ili9486l_lcd
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(ili9486l_lcd
  PUBLIC
    pico_stdlib
    hardware_dma
    hardware_spi
)

if(ILI9486L_LCD_BUILD_DEMO)
  add_subdirectory(demo)
endif()
