set(ILI9486L_LCD_DEMO_LOGO_PATH
  "${CMAKE_CURRENT_LIST_DIR}/assets/logo.jpg"
  CACHE FILEPATH
  "Path to the baseline JPEG asset embedded into the demo ELF"
)

if(NOT EXISTS "${ILI9486L_LCD_DEMO_LOGO_PATH}")
  message(FATAL_ERROR
    "Demo JPEG asset not found at ${ILI9486L_LCD_DEMO_LOGO_PATH}. "
    "Generate it with demo/tools/png_to_logo.py or set ILI9486L_LCD_DEMO_LOGO_PATH."
  )
endif()

add_executable(ili9486l_lcd_demo
  logo_jpg.S
  main.c
)

set_target_properties(ili9486l_lcd_demo PROPERTIES SUFFIX ".elf")

target_link_libraries(ili9486l_lcd_demo PRIVATE
  ili9486l::lcd
)

target_compile_definitions(ili9486l_lcd_demo PRIVATE
  LOGO_JPG_PATH=\"${ILI9486L_LCD_DEMO_LOGO_PATH}\"
)

if(ILI9486L_LCD_ENABLE_DEMO_STDIO_USB)
  pico_enable_stdio_usb(ili9486l_lcd_demo 1)
else()
  pico_enable_stdio_usb(ili9486l_lcd_demo 0)
endif()

if(ILI9486L_LCD_ENABLE_DEMO_STDIO_UART)
  pico_enable_stdio_uart(ili9486l_lcd_demo 1)
else()
  pico_enable_stdio_uart(ili9486l_lcd_demo 0)
endif()

pico_set_program_name(ili9486l_lcd_demo "ili9486l_lcd_demo")
pico_set_program_version(ili9486l_lcd_demo "1.0")
pico_add_extra_outputs(ili9486l_lcd_demo)
