#include "emu_file.h"

#if defined(PICO_ON_DEVICE)

#include "ff.h"

#include <stdlib.h>
#include <string.h>

struct emu_file {
    FIL fil;
};

static BYTE emu_mode_to_fatfs(const char *mode)
{
    if (!mode || !mode[0]) {
        return 0;
    }

    if (strchr(mode, 'r')) {
        if (strchr(mode, '+')) {
            return FA_READ | FA_WRITE;
        }
        return FA_READ;
    }

    if (strchr(mode, 'w')) {
        BYTE m = FA_CREATE_ALWAYS | FA_WRITE;
        if (strchr(mode, '+')) {
            m |= FA_READ;
        }
        return m;
    }

    if (strchr(mode, 'a')) {
        BYTE m = FA_OPEN_APPEND | FA_WRITE;
        if (strchr(mode, '+')) {
            m |= FA_READ;
        }
        return m;
    }

    return 0;
}

emu_file_t *emu_fopen(const char *path, const char *mode)
{
    emu_file_t *f;
    BYTE fat_mode;

    fat_mode = emu_mode_to_fatfs(mode);
    if (fat_mode == 0) {
        return NULL;
    }

    f = (emu_file_t *)calloc(1, sizeof(*f));
    if (!f) {
        return NULL;
    }

    if (f_open(&f->fil, path, fat_mode) != FR_OK) {
        free(f);
        return NULL;
    }

    return f;
}

int emu_fclose(emu_file_t *f)
{
    FRESULT fr;
    if (!f) {
        return 0;
    }
    fr = f_close(&f->fil);
    free(f);
    return (fr == FR_OK) ? 0 : -1;
}

size_t emu_fread(void *ptr, size_t size, size_t count, emu_file_t *f)
{
    UINT br = 0;
    UINT total = (UINT)(size * count);

    if (!f || size == 0 || count == 0) {
        return 0;
    }
    if (f_read(&f->fil, ptr, total, &br) != FR_OK) {
        return 0;
    }
    return br / size;
}

size_t emu_fwrite(const void *ptr, size_t size, size_t count, emu_file_t *f)
{
    UINT bw = 0;
    UINT total = (UINT)(size * count);

    if (!f || size == 0 || count == 0) {
        return 0;
    }
    if (f_write(&f->fil, ptr, total, &bw) != FR_OK) {
        return 0;
    }
    return bw / size;
}

int emu_fseek(emu_file_t *f, long offset, int whence)
{
    FSIZE_t pos;

    if (!f) {
        return -1;
    }

    switch (whence) {
    case EMU_SEEK_SET:
        if (offset < 0) {
            return -1;
        }
        pos = (FSIZE_t)offset;
        break;
    case EMU_SEEK_CUR: {
        long cur = (long)f_tell(&f->fil);
        if ((cur + offset) < 0) {
            return -1;
        }
        pos = (FSIZE_t)(cur + offset);
        break;
    }
    case EMU_SEEK_END: {
        long end = (long)f_size(&f->fil);
        if ((end + offset) < 0) {
            return -1;
        }
        pos = (FSIZE_t)(end + offset);
        break;
    }
    default:
        return -1;
    }

    return (f_lseek(&f->fil, pos) == FR_OK) ? 0 : -1;
}

long emu_ftell(emu_file_t *f)
{
    if (!f) {
        return -1;
    }
    return (long)f_tell(&f->fil);
}

int emu_fflush(emu_file_t *f)
{
    if (!f) {
        return -1;
    }
    return (f_sync(&f->fil) == FR_OK) ? 0 : -1;
}

int emu_feof(emu_file_t *f)
{
    if (!f) {
        return 1;
    }
    return f_eof(&f->fil) ? 1 : 0;
}

void emu_clearerr(emu_file_t *f)
{
    (void)f;
}

#else

#include <stdio.h>
#include <stdlib.h>

struct emu_file {
    FILE *fp;
};

emu_file_t *emu_fopen(const char *path, const char *mode)
{
    FILE *fp = fopen(path, mode);
    emu_file_t *f;

    if (!fp) {
        return NULL;
    }
    f = (emu_file_t *)malloc(sizeof(*f));
    if (!f) {
        fclose(fp);
        return NULL;
    }
    f->fp = fp;
    return f;
}

int emu_fclose(emu_file_t *f)
{
    int rc;
    if (!f) {
        return 0;
    }
    rc = fclose(f->fp);
    free(f);
    return rc;
}

size_t emu_fread(void *ptr, size_t size, size_t count, emu_file_t *f)
{
    if (!f) {
        return 0;
    }
    return fread(ptr, size, count, f->fp);
}

size_t emu_fwrite(const void *ptr, size_t size, size_t count, emu_file_t *f)
{
    if (!f) {
        return 0;
    }
    return fwrite(ptr, size, count, f->fp);
}

int emu_fseek(emu_file_t *f, long offset, int whence)
{
    int stdio_whence;
    if (!f) {
        return -1;
    }
    switch (whence) {
    case EMU_SEEK_SET:
        stdio_whence = SEEK_SET;
        break;
    case EMU_SEEK_CUR:
        stdio_whence = SEEK_CUR;
        break;
    case EMU_SEEK_END:
        stdio_whence = SEEK_END;
        break;
    default:
        return -1;
    }
    return fseek(f->fp, offset, stdio_whence);
}

long emu_ftell(emu_file_t *f)
{
    if (!f) {
        return -1;
    }
    return ftell(f->fp);
}

int emu_fflush(emu_file_t *f)
{
    if (!f) {
        return -1;
    }
    return fflush(f->fp);
}

int emu_feof(emu_file_t *f)
{
    if (!f) {
        return 1;
    }
    return feof(f->fp);
}

void emu_clearerr(emu_file_t *f)
{
    if (!f) {
        return;
    }
    clearerr(f->fp);
}

#endif
