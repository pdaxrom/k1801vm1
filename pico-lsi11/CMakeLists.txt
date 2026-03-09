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

project(pico_lsi11 C CXX ASM)

pico_sdk_init()

add_subdirectory(FatFs_SPI)

set(PICO_LSI11_DISPLAY_BACKEND "st7565" CACHE STRING
    "Display backend to build into pico-lsi11 (none, st7565, ili9486l)")
set_property(CACHE PICO_LSI11_DISPLAY_BACKEND PROPERTY STRINGS none st7565 ili9486l)
string(TOLOWER "${PICO_LSI11_DISPLAY_BACKEND}" PICO_LSI11_DISPLAY_BACKEND)

set(PICO_LSI11_DISPLAY_SOURCES
    display_backend.c
)
set(PICO_LSI11_DISPLAY_LIBRARIES)
set(PICO_LSI11_DISPLAY_DEFINITIONS)

if(PICO_LSI11_DISPLAY_BACKEND STREQUAL "none")
  list(APPEND PICO_LSI11_DISPLAY_DEFINITIONS
      PICO_LSI11_DISPLAY_BACKEND_NONE=1
  )
elseif(PICO_LSI11_DISPLAY_BACKEND STREQUAL "st7565")
  list(APPEND PICO_LSI11_DISPLAY_SOURCES
      st7565_lcd/term_display.c
  )
  list(APPEND PICO_LSI11_DISPLAY_DEFINITIONS
      PICO_LSI11_DISPLAY_BACKEND_ST7565=1
  )
elseif(PICO_LSI11_DISPLAY_BACKEND STREQUAL "ili9486l")
  set(ILI9486L_LCD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
  add_subdirectory(ili9486l_lcd)
  list(APPEND PICO_LSI11_DISPLAY_LIBRARIES
      ili9486l::lcd
      pico_sync
  )
  list(APPEND PICO_LSI11_DISPLAY_DEFINITIONS
      PICO_LSI11_DISPLAY_BACKEND_ILI9486L=1
  )
else()
  message(FATAL_ERROR
      "Unsupported PICO_LSI11_DISPLAY_BACKEND='${PICO_LSI11_DISPLAY_BACKEND}'. "
      "Expected one of: none, st7565, ili9486l")
endif()

set(PICO_LSI11_COMMON_SOURCES
    main.c
    hw_config.c
    ../core/core.c
    ../core/disas.c
    ../lsi11/adapter_core.c
    ../lsi11/bus.c
    ../lsi11/devio.c
    ../lsi11/irq.c
    ../lsi11/emu_file.c
    ../lsi11/options.c
    ../lsi11/ubmap.c
    ../lsi11/util_term.c
    ../lsi11/dev_dl11.c
    ../lsi11/dev_dz11.c
    ../lsi11/dev_kw11.c
    ../lsi11/dev_rl11.c
    ../lsi11/dev_rh11.c
    ../lsi11/dev_rk11.c
    ../lsi11/dev_xp.c
    ../lsi11/dev_tq11.c
    ../lsi11/dev_lp11.c
    ../lsi11/dev_sr.c
    ../lsi11/dev_vm1sel.c
    ../lsi11/dev_vm1sav.c
    ${PICO_LSI11_DISPLAY_SOURCES}
)

function(add_pico_emu_target target_name mmu_value machine_define)
  add_executable(${target_name}
      ${PICO_LSI11_COMMON_SOURCES}
  )

  target_include_directories(${target_name} PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}
      ${CMAKE_CURRENT_LIST_DIR}/../core
      ${CMAKE_CURRENT_LIST_DIR}/../lsi11
  )

  target_compile_definitions(${target_name} PRIVATE
      ENABLE_MMU=${mmu_value}
      PICO_MALLOC_PANIC=0
      ${machine_define}=1
      ${PICO_LSI11_DISPLAY_DEFINITIONS}
  )
  if("${mmu_value}" STREQUAL "0")
    target_compile_definitions(${target_name} PRIVATE
        MMU_STUB_REGS_WHEN_DISABLED=0
    )
  endif()

  target_link_libraries(${target_name} PRIVATE
      pico_stdlib
      pico_multicore
      hardware_sync
      hardware_spi
      FatFs_SPI
      ${PICO_LSI11_DISPLAY_LIBRARIES}
  )

  pico_set_program_name(${target_name} "${target_name}")
  pico_set_program_version(${target_name} "0.1")

  pico_enable_stdio_uart(${target_name} 0)
  pico_enable_stdio_usb(${target_name} 1)

  pico_add_extra_outputs(${target_name})
endfunction()

add_pico_emu_target(pico_lsi11 0 LSI11_TARGET_1104)
add_pico_emu_target(pico_pdp1184 1 LSI11_TARGET_1184)
