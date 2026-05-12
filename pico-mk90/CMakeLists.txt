cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Check for custom cross environment settings
if(EXISTS $ENV{HOME}/cross-env.cmake)
  message(STATUS "Custom cross environment detected")
  include($ENV{HOME}/cross-env.cmake)
else()
  message(STATUS "Custom cross environment is not detected")
endif()

include(pico_sdk_import.cmake)

project(pico_mk90 C CXX ASM)

pico_sdk_init()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../pico-lsi11/FatFs_SPI FatFs_SPI)

set(PICO_MK90_STEPS_PER_FRAME "2000" CACHE STRING
    "PDP-11 core steps executed per LCD frame")
set(PICO_MK90_FRAME_US "16667" CACHE STRING
    "Frame period in microseconds")
set(PICO_MK90_KEY_HOLD_FRAMES "2" CACHE STRING
    "stdin key tap hold duration in rendered frames")
set(PICO_MK90_KEY_GAP_FRAMES "1" CACHE STRING
    "empty frames inserted between stdin key taps")
set(PICO_MK90_SMP_SLOT_SIZE "32768" CACHE STRING
    "Maximum SD-loaded SMP image bytes per slot")
set(PICO_MK90_LCD_X_OFFSET "4" CACHE STRING
    "Horizontal offset for the 120-pixel MK90 LCD inside a 128-pixel ST7565")

add_executable(pico_mk90
    main.c
    pico_mk90_display.c
    pico_mk90_media.c
    ../pico-lsi11/hw_config.c
    ../core/core.c
    ../core/disas.c
    ../bk0010-01/bk_timing.c
    ../mk90/mk90_machine.c
    ../mk90/mk90_lcd.c
    ../mk90/mk90_rtc.c
    ../mk90/mk90_io.c
    ../mk90/mk90_syscon.c
    ../mk90/mk90_smp.c
    ../mk90/mk90_keyboard.c
)

target_include_directories(pico_mk90 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../core
    ${CMAKE_CURRENT_LIST_DIR}/../bk0010-01
    ${CMAKE_CURRENT_LIST_DIR}/../mk90
)

target_compile_definitions(pico_mk90 PRIVATE
    ENABLE_MMU=0
    PICO_MALLOC_PANIC=0
    PICO_ON_DEVICE=1
    PICO_MK90_STEPS_PER_FRAME=${PICO_MK90_STEPS_PER_FRAME}
    PICO_MK90_FRAME_US=${PICO_MK90_FRAME_US}
    PICO_MK90_KEY_HOLD_FRAMES=${PICO_MK90_KEY_HOLD_FRAMES}
    PICO_MK90_KEY_GAP_FRAMES=${PICO_MK90_KEY_GAP_FRAMES}
    PICO_MK90_SMP_SLOT_SIZE=${PICO_MK90_SMP_SLOT_SIZE}
    PICO_MK90_LCD_X_OFFSET=${PICO_MK90_LCD_X_OFFSET}
)

target_compile_options(pico_mk90 PRIVATE
    -Wall
    -Wextra
)

target_link_libraries(pico_mk90 PRIVATE
    pico_stdlib
    hardware_spi
    hardware_dma
    FatFs_SPI
)

pico_set_program_name(pico_mk90 "pico_mk90")
pico_set_program_version(pico_mk90 "0.1")

pico_enable_stdio_uart(pico_mk90 0)
pico_enable_stdio_usb(pico_mk90 1)

pico_add_extra_outputs(pico_mk90)
