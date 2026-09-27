/* uJ11 SD label v1. Decode bytes explicitly: never cast packed disk structures. */
#ifndef UJ11_SD_LABEL_H
#define UJ11_SD_LABEL_H
#include <stdint.h>
#define SD_PARTS 14
#define SD_MENU 1
#define SD_BOOT 1
#define SD_READONLY 2
struct sd_partition { uint32_t start, blocks; uint8_t kind, unit, media, flags, mode; };
struct sd_label { uint32_t blocks, features; unsigned count; struct sd_partition part[SD_PARTS]; };
int sd_label_decode(const uint8_t raw[512], uint32_t capacity, struct sd_label *out);
#endif
