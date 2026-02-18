#ifndef EMU_FILE_H_
#define EMU_FILE_H_

#include <stddef.h>

#define EMU_SEEK_SET 0
#define EMU_SEEK_CUR 1
#define EMU_SEEK_END 2

typedef struct emu_file emu_file_t;

emu_file_t *emu_fopen(const char *path, const char *mode);
int emu_fclose(emu_file_t *f);

size_t emu_fread(void *ptr, size_t size, size_t count, emu_file_t *f);
size_t emu_fwrite(const void *ptr, size_t size, size_t count, emu_file_t *f);

int emu_fseek(emu_file_t *f, long offset, int whence);
long emu_ftell(emu_file_t *f);
int emu_fflush(emu_file_t *f);

int emu_feof(emu_file_t *f);
void emu_clearerr(emu_file_t *f);

#endif
