#include "pico_mk90_media.h"

#include "f_util.h"
#include "ff.h"
#include "mk90_defs.h"
#include "mk90_machine.h"

#include <stdio.h>
#include <string.h>

#ifndef PICO_MK90_SMP_SLOT_SIZE
#define PICO_MK90_SMP_SLOT_SIZE 32768u
#endif

static byte smp_storage[2][PICO_MK90_SMP_SLOT_SIZE];

static int is_missing(FRESULT fr)
{
    return fr == FR_NO_FILE || fr == FR_NO_PATH;
}

static int read_file_prefix(const char *path, byte *dst, size_t max_size,
                            int allow_truncate, size_t *read_count,
                            char *err, size_t err_len)
{
    FIL file;
    FRESULT fr;
    FSIZE_t file_size;
    UINT to_read;
    UINT br = 0;

    if (read_count) {
        *read_count = 0;
    }

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        if (is_missing(fr)) {
            return 1;
        }
        if (err && err_len) {
            snprintf(err, err_len, "%s: f_open failed: %s (%d)",
                     path, FRESULT_str(fr), fr);
        }
        return -1;
    }

    file_size = f_size(&file);
    if (file_size > (FSIZE_t)max_size && !allow_truncate) {
        if (err && err_len) {
            snprintf(err, err_len, "%s is too large: %lu > %lu bytes",
                     path, (unsigned long)file_size, (unsigned long)max_size);
        }
        (void)f_close(&file);
        return -1;
    }

    to_read = (UINT)((file_size > (FSIZE_t)max_size) ? max_size : file_size);
    fr = f_read(&file, dst, to_read, &br);
    (void)f_close(&file);
    if (fr != FR_OK || br != to_read) {
        if (err && err_len) {
            snprintf(err, err_len, "%s: f_read failed: %s (%d), %u/%u bytes",
                     path, FRESULT_str(fr), fr, (unsigned)br,
                     (unsigned)to_read);
        }
        return -1;
    }

    if (read_count) {
        *read_count = br;
    }
    return 0;
}

static int read_first_path(const char *primary, const char *fallback,
                           byte *dst, size_t max_size, int allow_truncate,
                           int required, size_t *read_count,
                           char *err, size_t err_len)
{
    int rc = read_file_prefix(primary, dst, max_size, allow_truncate,
                              read_count, err, err_len);

    if (rc == 0 || rc < 0) {
        return rc;
    }

    if (fallback && fallback[0] != '\0') {
        rc = read_file_prefix(fallback, dst, max_size, allow_truncate,
                              read_count, err, err_len);
        if (rc == 0 || rc < 0) {
            return rc;
        }
    }

    if (required) {
        if (err && err_len) {
            if (fallback && fallback[0] != '\0') {
                snprintf(err, err_len, "missing %s or %s", primary, fallback);
            } else {
                snprintf(err, err_len, "missing %s", primary);
            }
        }
        return -1;
    }

    if (read_count) {
        *read_count = 0;
    }
    return 1;
}

static int load_smp_slot(mk90_state_t *state, unsigned slot_index,
                         const char *primary, const char *fallback,
                         char *err, size_t err_len)
{
    mk90_smp_slot_t *slot = &state->smp[slot_index];
    size_t read_count = 0;
    int rc;

    memset(slot, 0, sizeof(*slot));
    rc = read_first_path(primary, fallback, smp_storage[slot_index],
                         sizeof(smp_storage[slot_index]), 0, 0,
                         &read_count, err, err_len);
    if (rc < 0) {
        return -1;
    }
    if (read_count == 0) {
        return 0;
    }

    slot->data = smp_storage[slot_index];
    slot->size = read_count;
    slot->mask = (slot->size < 0100000u) ? 0177777u : 077777777u;
    slot->present = 1;
    slot->dirty = 0;
    slot->owns_data = 0;
    slot->position = 0;
    slot->cmd = 0;
    slot->path[0] = '\0';
    printf("SMP%u: loaded %lu bytes from SD\n", slot_index,
           (unsigned long)read_count);
    return 0;
}

int pico_mk90_load_sd_images(char *err, size_t err_len)
{
    mk90_state_t *state = mk90_machine_state();
    size_t read_count = 0;

    memset(state->rom, 0377, sizeof(state->rom));

    if (read_first_path("0:/roms/romt.bin", "0:/romt.bin",
                        state->rom, MK90_ROM_TEST_SIZE, 1, 0,
                        &read_count, err, err_len) < 0) {
        return -1;
    }
    if (read_count != 0) {
        printf("ROMT: loaded %lu bytes from SD\n", (unsigned long)read_count);
    } else {
        printf("ROMT: not present, test ROM area left blank\n");
    }

    if (read_first_path("0:/roms/rom.bin", "0:/rom.bin",
                        &state->rom[MK90_ROM_MAIN_OFFSET],
                        MK90_ROM_MAIN_MAX, 1, 1,
                        &read_count, err, err_len) < 0) {
        return -1;
    }
    if (read_count == 0) {
        if (err && err_len) {
            snprintf(err, err_len, "main ROM is empty");
        }
        return -1;
    }
    printf("ROM: loaded %lu bytes from SD\n", (unsigned long)read_count);

    if (load_smp_slot(state, 0u, "0:/media/smp0.bin", "0:/smp0.bin",
                      err, err_len) != 0) {
        return -1;
    }
    if (load_smp_slot(state, 1u, "0:/media/smp1.bin", "0:/smp1.bin",
                      err, err_len) != 0) {
        return -1;
    }

    return 0;
}
