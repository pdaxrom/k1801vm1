#ifndef MK90_DEFS_H
#define MK90_DEFS_H

#include "../core/core.h"

#define MK90_ADDR_SPACE_SIZE 0200000u

#define MK90_RAM_MIN_SIZE    040000u
#define MK90_RAM_MAX_SIZE    0100000u

#define MK90_ROM_START       040000u
#define MK90_ROM_SIZE        0137400u
#define MK90_ROM_TEST_SIZE   040000u
#define MK90_ROM_MAIN_OFFSET 040000u
#define MK90_ROM_MAIN_MAX    0077400u

#define MK90_LCD_BASE        0164000u /* 0xE800 */
#define MK90_IO_BASE         0164020u /* 0xE810 */
#define MK90_SYS_BASE        0164032u /* 0xE81A */
#define MK90_RTC_BASE        0165000u /* 0xEA00 */

#define MK90_LCD_CONFIG_INIT 0104306u /* 0x88C6 */
#define MK90_RESET_PC        0173000u /* 0xF600 */

#define MK90_VEC_EVNT        000100u /* 0x0040 */
#define MK90_VEC_C0          000300u /* 0x00C0 */
#define MK90_VEC_C4          000304u /* 0x00C4 */
#define MK90_VEC_C8          000310u /* 0x00C8 */

#define MK90_SCREEN_WIDTH    120
#define MK90_SCREEN_HEIGHT   64
#define MK90_SCREEN_BYTES    960

#define MK90_IMAGE_PATH_MAX  1024

#endif
