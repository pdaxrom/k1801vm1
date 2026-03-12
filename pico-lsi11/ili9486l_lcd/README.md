# ILI9486L LCD library and demo for Raspberry Pi Pico

Проект теперь разделён на библиотеку и демонстрационное приложение:

- `ili9486l_lcd` - статическая библиотека для дисплея, baseline JPEG и VT100-терминала
- `ili9486l::lcd` - alias target для `target_link_libraries(...)`
- `ili9486l_lcd_demo` - demo app из `demo/`

Публичные заголовки лежат в `include/`:

- `include/ili9486l.h`
- `include/ili9486l_jpeg.h`
- `include/vt100_terminal.h`

Внутренние файлы вроде `font5x7.*` и `tjpgd/*` остаются приватными деталями реализации.

Demo-специфичные файлы вынесены отдельно:

- `demo/main.c`
- `demo/logo_jpg.S`
- `demo/assets/*`
- `demo/CMakeLists.txt`

Корневой `CMakeLists.txt` теперь описывает библиотеку и только при `ILI9486L_LCD_BUILD_DEMO=ON` подключает `demo/` через `add_subdirectory(demo)`.

## Подключение дисплея

Используемые сигналы в demo:

| Дисплей | RP2040 |
| --- | --- |
| `CLK` | `GP14` |
| `MOSI` | `GP15` |
| `RES` | `GP11` |
| `DC` | `GP10` |
| `GND` | `GND` |
| `VCC` | `3V3(OUT)`* |
| `BLK` | `3V3(OUT)`** |

\* Для чистого ILI9486L обычно нужен `3.3V`, но проверьте требования своего модуля.

\** В demo управление подсветкой не используется. Если нужно управлять ей с GPIO, достаточно поменять конфиг в `src/ili9486l.c`.

`CS` в проекте не используется: предполагается, что модуль уже постоянно выбран на SPI-шине.

## Standalone build

Для standalone-сборки проект по-прежнему умеет подтягивать `$HOME/cross-env.cmake`. Если `PICO_SDK_PATH` не передан из родительского проекта, он берётся из:

- `$HOME/cross-env.cmake`
- или переменной окружения `PICO_SDK_PATH`

Сборка demo:

```bash
cmake -S . -B build
cmake --build build -j4
```

Если нужен только library target без demo:

```bash
cmake -S . -B build -DILI9486L_LCD_BUILD_DEMO=OFF
cmake --build build -j4
```

Если каталог `build/` был сконфигурирован старой версией `CMakeLists.txt` и закэшировал host compiler (`/usr/bin/cc`), его нужно пересоздать или использовать новый build directory.

Артефакты demo:

- `build/ili9486l_lcd_demo.elf`
- `build/ili9486l_lcd_demo.uf2`

Для `pico2`:

```bash
cmake -S . -B build-pico2 -DPICO_BOARD=pico2
cmake --build build-pico2 -j4
```

## Использование как submodule

Если Pico SDK уже импортирован и инициализирован в родительском проекте, можно просто делать `add_subdirectory(...)`.

В родительском проекте порядок должен быть стандартный для Pico SDK:

1. определить `PICO_SDK_PATH`
2. сделать `include(.../pico_sdk_import.cmake)`
3. вызвать `project(...)`
4. вызвать `pico_sdk_init()`
5. подключить этот проект через `add_subdirectory(...)`

Пример:

```cmake
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(my_app C CXX ASM)
pico_sdk_init()

set(ILI9486L_LCD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ili9486l_lcd)

target_link_libraries(my_app PRIVATE ili9486l::lcd)
```

Если demo в родительском проекте не нужен, оставляйте:

```cmake
set(ILI9486L_LCD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
```

Полезные опции:

- `ILI9486L_LCD_BUILD_DEMO` - собрать demo из `demo/`
- `ILI9486L_LCD_ENABLE_DEMO_STDIO_USB` - включить USB CDC для demo
- `ILI9486L_LCD_ENABLE_DEMO_STDIO_UART` - включить UART stdio для demo
- `ILI9486L_LCD_DEMO_RUN_FPS_TEST` - запускать on-device тест FPS для полной перерисовки экрана при старте demo
- `ILI9486L_LCD_DEMO_LOGO_PATH` - путь к baseline JPEG, который будет вшит в demo ELF

Если `demo/assets/logo.jpg` отсутствует, можно либо сгенерировать его скриптами ниже, либо передать альтернативный путь через `-DILI9486L_LCD_DEMO_LOGO_PATH=/abs/path/logo.jpg`.

## Library-only integration

Если demo не нужен и проект нужен только как библиотека дисплея, JPEG и VT100, минимальная схема такая:

```cmake
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(my_app C CXX ASM)
pico_sdk_init()

set(ILI9486L_LCD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ili9486l_lcd)

add_executable(my_app
  src/main.c
)

target_link_libraries(my_app PRIVATE
  ili9486l::lcd
)

pico_add_extra_outputs(my_app)
```

Минимальный `main.c`:

```c
#include "ili9486l.h"

int main(void) {
  ili9486l_init();
  ili9486l_fill_screen(LCD_COLOR_BLACK);
  ili9486l_draw_string(16, 16, "HELLO", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 2);

  while (1) {
    tight_loop_contents();
  }
}
```

Если нужны дополнительные подсистемы библиотеки:

- `#include "ili9486l.h"` - базовый драйвер дисплея и примитивы рисования
- `#include "ili9486l_jpeg.h"` - baseline JPEG из памяти / flash
- `#include "vt100_terminal.h"` - VT100/ANSI/VT52 терминал

Текущий драйвер всё ещё привязан к compile-time конфигу SPI и пинов. Если в родительском проекте нужны другие пины или другой SPI controller, это задаётся compile definitions на target библиотеки:

```cmake
target_compile_definitions(ili9486l_lcd PRIVATE
  LCD_SPI_PORT=spi0
  LCD_SPI_BAUDRATE_HZ=(40u*1000u*1000u)
  LCD_PIN_SCK=2
  LCD_PIN_MOSI=3
  LCD_PIN_RST=4
  LCD_PIN_DC=5
  LCD_PIN_BLK=6
)
```

Поддерживаемые compile-time настройки сейчас такие:

- `LCD_SPI_PORT`
- `LCD_SPI_BAUDRATE_HZ`
- `LCD_PIN_SCK`
- `LCD_PIN_MOSI`
- `LCD_PIN_RST`
- `LCD_PIN_DC`
- `LCD_PIN_BLK`

## Работа с дисплеем

Публичный API: `include/ili9486l.h`

Что даёт библиотека:

- инициализацию ILI9486L в `landscape` режиме `480x320`
- базовые примитивы: `draw_pixel`, `fill_rect`, `fill_screen`, `draw_char`, `draw_string`
- streaming path без framebuffer через:
  - `ili9486l_begin_write()`
  - `ili9486l_write_rgb666_pixels()`
  - `ili9486l_write_rgb666_wire_pixels()`
  - `ili9486l_write_rgb888_as_rgb666_pixels()`
- прямой вывод прямоугольников в `RGB666`
- аппаратный vertical scroll (`VSCRDEF` / `VSCRSADD`)

Внутренний формат библиотеки - `RGB666`.
Если входящие данные `RGB888`, библиотека сжимает их до `RGB666` при отправке в дисплей.

Горячий путь вывода оптимизирован под low-RAM режим:

- без полного framebuffer
- с DMA для крупных SPI-передач
- с raw on-wire path для уже упакованных `RGB666` данных

## Работа с JPEG

Публичный API: `include/ili9486l_jpeg.h`

Доступные функции:

```c
bool ili9486l_jpeg_get_info(const uint8_t *jpeg_data, size_t jpeg_size, ili9486l_jpeg_info_t *out_info);
bool ili9486l_jpeg_draw(const uint8_t *jpeg_data, size_t jpeg_size, uint16_t x, uint16_t y);
```

Свойства JPEG-пути:

- используется `TJpgDec`
- декодирование идёт MCU-блоками без full-frame буфера
- входные данные читаются прямо из flash / ROM / `const uint8_t[]`
- декодер выдаёт `RGB888`, библиотека сразу пишет это в дисплей как `RGB666`

Ограничения JPEG:

- только `baseline JPEG`
- `progressive JPEG` не поддерживается
- `ili9486l_jpeg_draw()` вернёт `false`, если изображение не помещается в текущую геометрию дисплея

Demo app использует этот API для boot logo, а сам JPEG-файл `demo/assets/logo.jpg` линкуется в ELF через `.incbin` в `demo/logo_jpg.S`.

## VT100 терминал

Публичный API: `include/vt100_terminal.h`

Терминал рассчитан на дисплей `480x320`:

- `80` колонок
- `35` строк
- ячейка `6x9`
- глиф `5x7` с верхним и нижним зазором

Основные функции:

```c
void vt100_terminal_init(vt100_terminal_t *terminal, uint16_t origin_x, uint16_t origin_y);
void vt100_terminal_set_getch_hook(vt100_terminal_t *terminal, vt100_terminal_getch_hook_fn getch_hook, void *user_data);
bool vt100_terminal_getch(vt100_terminal_t *terminal, int ch);
void vt100_terminal_set_output(vt100_terminal_t *terminal, vt100_terminal_output_fn output_fn, void *user_data);
void vt100_terminal_putc(vt100_terminal_t *terminal, char ch);
void vt100_terminal_write_n(vt100_terminal_t *terminal, const char *text, size_t len);
void vt100_terminal_write(vt100_terminal_t *terminal, const char *text);
void vt100_terminal_tick(vt100_terminal_t *terminal, uint32_t elapsed_ms);
void vt100_terminal_render(vt100_terminal_t *terminal);
```

Что реализовано:

- потоковый приём символов по одному байту
- приём буфера фиксированной длины через `vt100_terminal_write_n(...)`
- отдельный путь локального ввода через `vt100_terminal_getch()`
- `getch` hook через `vt100_terminal_set_getch_hook(...)` для локального перехвата ввода перед `vt100_terminal_putc()`
- символьный screen buffer без framebuffer дисплея
- subset `VT100`, `ANSI`, `DEC Special Graphics`, `UK charset`, `VT52`
- `SGR` цвета и атрибуты
- `DECOM`, `DECAWM`, `DECSCNM`, `IRM`, `LMN`, scroll region, tab stops
- output callback для `DA` / `DSR` ответов обратно хосту
- blinking attribute через `vt100_terminal_tick(elapsed_ms)`

Через `vt100_terminal_getch()` включается встроенная terminal UI-обвязка:

- последняя строка резервируется под status line
- доступны режимы `SCROLL` и `PAGED`
- `Ctrl+E` включает локальный префикс-командный режим
- `Ctrl+E 1`, `Ctrl+E 2`, `Ctrl+E 3` переключают `80x34`, `80x30`, `80x24`
- `Ctrl+E s` и `Ctrl+E p` переключают `SCROLL` и `PAGED`
- если настроен `vt100_terminal_set_output(...)`, то в `PAGED` режиме библиотека отправляет `XOFF` (`Ctrl+S`, `0x13`) при остановке страницы и `XON` (`Ctrl+Q`, `0x11`) после `Space`

Терминал не претендует на полный VT100. Актуальные пробелы перечислены в `TODO.md`.

### Как правильно интегрировать терминал

Правильный жизненный цикл такой:

1. Инициализировать LCD через `ili9486l_init()`.
2. Создать `vt100_terminal_t` и вызвать `vt100_terminal_init(...)`.
3. Если хост ожидает ответы терминала (`DA`, `DSR`), подключить `vt100_terminal_set_output(...)`.
4. Подавать входные байты:
   - `vt100_terminal_write_n(...)` или `vt100_terminal_putc(...)` для обычного потока данных терминала
   - `vt100_terminal_getch(...)` для локального пользовательского ввода
5. Периодически вызывать `vt100_terminal_tick(elapsed_ms)` для `blink`

Минимальный пример:

```c
#include "ili9486l.h"
#include "vt100_terminal.h"

#include "pico/stdlib.h"

static vt100_terminal_t g_terminal;

static void terminal_output(const char *data, size_t len, void *user_data)
{
    (void)user_data;

    for (size_t i = 0; i < len; ++i) {
        putchar_raw((uint8_t)data[i]);
    }
}

int main(void)
{
    uint32_t last_ms;

    stdio_init_all();
    ili9486l_init();

    vt100_terminal_init(&g_terminal, 0u, 0u);
    vt100_terminal_set_output(&g_terminal, terminal_output, NULL);
    (void)vt100_terminal_getch(&g_terminal, PICO_ERROR_TIMEOUT);
    vt100_terminal_write(&g_terminal, "\x1b[2J\x1b[HREADY\r\n");

    last_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

    while (true) {
        const int ch = getchar_timeout_us(0);
        const uint32_t now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

        vt100_terminal_tick(&g_terminal, now_ms - last_ms);
        last_ms = now_ms;

        (void)vt100_terminal_getch(&g_terminal, ch);

        tight_loop_contents();
    }
}
```

### Для чего нужны `vt100_terminal_getch()` и `vt100_terminal_set_getch_hook()`

`vt100_terminal_putc()` всегда сразу отдаёт байт в parser терминала.

`vt100_terminal_getch()` нужен для локального пользовательского ввода. Он принимает `int`, чтобы ему можно было напрямую передавать результат `getchar_timeout_us(0)`.

Возвращаемое значение:

- `true`: байт дошёл до terminal parser
- `false`: символа не было или байт был использован встроенной логикой терминала / локальным hook

Типичная схема такая:

- удалённый поток терминальных данных, который уже является содержимым terminal session:
  - подавать через `vt100_terminal_putc()` или `vt100_terminal_write_n()`
- локальные нажатия клавиш пользователя:
  - подавать через `vt100_terminal_getch()`

То есть:

- `vt100_terminal_putc()` и `vt100_terminal_write_n()` это путь `data -> terminal`
- `vt100_terminal_getch()` это путь `keyboard/input -> built-in terminal UI + optional local hook -> terminal`

`vt100_terminal_set_getch_hook()` ставит callback, который вызывается внутри `vt100_terminal_getch()` после встроенных локальных команд и до передачи байта в обычный terminal parser.

Сама `vt100_terminal_getch()` уже обрабатывает встроенные terminal UI-клавиши:

- `Ctrl+E` войти в режим локальной команды
- `Ctrl+E 1`, `Ctrl+E 2`, `Ctrl+E 3` сменить размер терминала
- `Ctrl+E s` и `Ctrl+E p` сменить режим scroll/paged
- `Space` для перехода на следующую страницу в `PAGED`
- остальные локальные клавиши во время ожидания страницы съедаются локально и не попадают в очередь удалённых данных

`vt100_terminal_set_getch_hook()` нужен сверх этого, когда часть локального ввода должна обрабатываться самим приложением, например:

- временно переключить режим ввода
- перехватить стрелки или горячие клавиши
- заблокировать передачу некоторых клавиш в удалённую terminal session

Hook ставится через `vt100_terminal_set_getch_hook(...)` и работает так:

- вернуть `true`, если байт полностью обработан локально и не должен идти в terminal parser
- вернуть `false`, если байт нужно передать дальше в обычный `vt100_terminal_putc()`

Пример дополнительного локального hotkey:

```c
static bool terminal_getch_hook(vt100_terminal_t *terminal, char ch, void *user_data)
{
    bool *clear_requested = (bool *)user_data;

    (void)terminal;

    if ((unsigned char)ch == 0x0Cu) {
        *clear_requested = true;
        return true;
    }

    return false;
}
```

Подключение:

```c
bool clear_requested = false;

vt100_terminal_set_getch_hook(&g_terminal, terminal_getch_hook, &clear_requested);
```

После этого поток локального ввода нужно подавать уже через `vt100_terminal_getch(&g_terminal, ch)`, а не через `vt100_terminal_putc(...)`.

Коротко:

- нет локальных hotkeys или UI-перехвата: используй `vt100_terminal_putc()` / `vt100_terminal_write_n()`
- есть локальные hotkeys поверх терминала: ставь `vt100_terminal_set_getch_hook()` и подавай ввод через `vt100_terminal_getch()`

### Что важно не перепутать

- `vt100_terminal_init()` сразу очищает и рисует область терминала на LCD, поэтому вызывать его нужно после `ili9486l_init()`
- `vt100_terminal_write_n()` удобен для буферов с фиксированной длиной, в том числе если внутри есть `'\0'`
- `vt100_terminal_set_output()` нужен только если терминал должен отвечать хосту escape-последовательностями
- встроенная terminal UI активируется через `vt100_terminal_getch()`, поэтому если нужны status line, paging и `Ctrl+E`-команды, локальный ввод надо подавать именно через `vt100_terminal_getch()`, а не напрямую в `vt100_terminal_putc()`
- если вы резервируете часть экрана под свою панель состояния, нужно самостоятельно ограничить рабочую scroll-область и не давать хосту писать поверх неё

## Demo app

`demo/main.c` считается именно демонстрационным приложением, а не "центром" проекта.

Что делает demo:

1. Инициализирует дисплей.
2. Показывает встроенный `demo/assets/logo.jpg`.
3. По умолчанию прогоняет on-device benchmark: полный redraw экрана, полный `vt100_terminal_render()` и terminal scroll path.
4. Активирует встроенную terminal UI-обвязку первым вызовом `vt100_terminal_getch()`.
5. Запускает терминал с рабочей областью `80x34` и фиксированной статусной строкой снизу.
6. Поддерживает локальные режимы `SCROLL` и `PAGED`.
7. Поддерживает локальные команды `Ctrl+E 1/2/3/s/p`.
8. Читает символы из `stdio`.
9. Обновляет blink и служебные состояния в loop.

Demo использует и USB CDC, и UART stdio по умолчанию. Это можно отключить CMake-опциями `ILI9486L_LCD_ENABLE_DEMO_STDIO_USB` и `ILI9486L_LCD_ENABLE_DEMO_STDIO_UART`.

Локальные элементы demo:

- последняя строка экрана всегда занята статусом
- размер рабочей области переключается между `80x34`, `80x30` и `80x24`
- в `PAGED` режиме перед новым листом status line просит нажать `Space`
- `Ctrl+E 1`, `Ctrl+E 2`, `Ctrl+E 3` переключают размер
- `Ctrl+E s` и `Ctrl+E p` переключают режим `SCROLL` / `PAGED`

Demo использует только public `vt100_terminal_getch()` и не держит реализацию status line / paging в `main.c`.

Пример отключения benchmark:

```bash
cmake -S . -B build -DILI9486L_LCD_DEMO_RUN_FPS_TEST=OFF
cmake --build build -j4
```

Во время benchmark demo делает три замера и печатает результаты в `stdio`:

- полный `480x320` redraw через public streaming API библиотеки
- полный `vt100_terminal_render()` для заполненного экрана `80x35`
- terminal scroll path через серию новых `80`-колоночных строк

Эти числа удобны как baseline после изменений в SPI path, DMA и VT100 renderer.

## Подготовка boot logo

Подготовка JPEG оставлена как отдельный pipeline через:

- `demo/tools/png_to_logo.py`
- `demo/tools/logo_from_magick.sh`
- `demo/assets/convert.sh`

Нужна `Pillow`:

```bash
python3 -m pip install Pillow
```

Пример:

```bash
python3 demo/tools/png_to_logo.py logo.png
python3 demo/tools/png_to_logo.py logo.png --rotate cw
```

Итоговый файл нужно положить в `demo/assets/logo.jpg`. Demo подхватывает его на этапе линковки.

## Host tests

Есть отдельные native tests для VT100-парсера:

```bash
cmake -S tests/host -B /tmp/ili9486l_lcd-build-host
cmake --build /tmp/ili9486l_lcd-build-host -j4
ctest --test-dir /tmp/ili9486l_lcd-build-host --output-on-failure
```

Они не требуют Pico SDK и не требуют железа.

## Ограничения

- библиотека ориентирована на один конкретный контроллер и текущий конфиг SPI в `src/ili9486l.c`
- публичный шрифтовой API не экспортируется: встроенный `5x7` используется только внутренне
- UTF-8 и scrollback в терминале пока не реализованы
- `DEC Special Graphics` покрывает практичный subset, а не весь исторический терминальный стек
- порядок цветов сейчас настроен как `BGR`, потому что на текущем модуле это даёт корректные цвета
