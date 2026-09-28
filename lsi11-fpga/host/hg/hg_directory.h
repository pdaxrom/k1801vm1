#ifndef HG_DIRECTORY_H
#define HG_DIRECTORY_H

#include <stdint.h>

int hg_directory_prepare(const char *directory, const char *image_path,
                         uint32_t blocks);
int hg_directory_export(const char *directory, const char *image_path);

#endif
