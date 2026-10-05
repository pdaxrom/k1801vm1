#ifndef BK_FDD_H
#define BK_FDD_H

#include <stdint.h>

#define BK_FDD_SECTORS 1600
#define BK_FDD_WORDS 3125

typedef struct {
	uint32_t base;
	uint32_t length;
	int readonly;
} bk_disk_partition;

/* Backends address absolute SD sectors, never filesystem disk-image files. */
typedef int (*bk_sd_read)(void *context, uint32_t lba, uint8_t data[512]);
typedef int (*bk_sd_write)(void *context, uint32_t lba, const uint8_t data[512]);

int bk_fdd_configure(uint32_t capacity, uint32_t boot_base, uint32_t boot_length,
                     const bk_disk_partition partitions[4], bk_sd_read read_sector,
                     bk_sd_write write_sector, void *context);
void bk_fdd_reset(void);
void bk_fdd_advance(unsigned int cycles, unsigned int mhz);
uint16_t bk_fdd_read(unsigned int address);
void bk_fdd_write(unsigned int address, uint16_t value);
int bk_fdd_flush(void);
int bk_fdd_error(void);

#endif
