cmake_minimum_required(VERSION 3.16)

project(vt100_terminal_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(vt100_terminal_tests
  ili9486l_stub.c
  vt100_terminal_test.c
  ../../src/font5x7.c
  ../../src/font5x7_row_masks.c
  ../../src/vt100_terminal.c
  ../../src/vt100_terminal_console.c
)

target_include_directories(vt100_terminal_tests PRIVATE
  ../../include
  ../../src
)

add_test(NAME vt100_terminal_tests COMMAND vt100_terminal_tests)
