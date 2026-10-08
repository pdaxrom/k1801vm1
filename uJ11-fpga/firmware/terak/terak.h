#ifndef UJ11_TERAK_H
#define UJ11_TERAK_H
#include "../storage/storage.h"
#ifndef UJ11_TERAK_DMA_BYTES
#define UJ11_TERAK_DMA_BYTES 57344u
#endif
struct terak_media {
	u32 start, blocks;
	uint8_t heads, readonly, format;
};
enum { MEDIA_FD, RAW_SSSD, RAW_DSSD, RAW_SSDD, RAW_DSDD, RAW_TERAK_SSDD, RAW_TERAK_DSDD, RAW_RL01, RAW_RL02 };
#ifdef UJ11_TERAK_RL
#define TERAK_UNITS 8
#else
#define TERAK_UNITS 4
#endif
extern struct terak_media media[4];
extern volatile uint8_t *const sector;
#ifdef UJ11_TERAK_RL
/* TRK1 header plus all 32 eight-byte sector entries; tail is reserved. */
#define TERAK_TRACK_META_BYTES 288
#else
#define TERAK_TRACK_META_BYTES 512
#endif
extern uint8_t track_meta[TERAK_TRACK_META_BYTES];
int fetch(u32 lba);
int store(u32 lba);
u32 word(unsigned offset);
u32 media_crc(const volatile uint8_t *data);
int track_load(unsigned unit, unsigned cylinder, unsigned head);
int track_save(unsigned unit, unsigned cylinder, unsigned head);
u32 track_data(unsigned unit, unsigned cylinder, unsigned head);
int track_format(unsigned unit, unsigned cylinder, unsigned head);
int media_open(unsigned unit, u32 start, u32 blocks, unsigned format, unsigned readonly);
int media_mount(u32 capacity);
int mbr_mount(u32 capacity);
#ifdef UJ11_TERAK_BIOS
#ifdef __riscv
#define TERAK_COLD __attribute__((section(".cold_bss")))
#else
#define TERAK_COLD
#endif
int mbr_select(unsigned profile);
int mbr_bios(void);
int mbr_settings(void);
unsigned mbr_summary(unsigned unit);
int fat_open(const char *name, u32 start, u32 blocks, unsigned limit);
int fat_read(void);
u32 fat_setting(unsigned slot, unsigned writing);
int bios_load(void);
int bios_poll(void);
void publish_media(unsigned menu);
#endif
void qb_poll(void);
void qb_service(void);
#ifdef UJ11_TERAK_RL
extern struct terak_media rl_media[4];
extern unsigned rl_irq;
void disk_irq(unsigned qb_irq);
void qb_publish(void);
void rl_reset(void);
int rl_poll(u32 request);
void rl_service(void);
#endif
#endif
