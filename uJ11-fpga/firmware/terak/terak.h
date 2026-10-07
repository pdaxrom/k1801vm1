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
enum { MEDIA_FD, RAW_SSSD, RAW_DSSD, RAW_SSDD, RAW_DSDD, RAW_TERAK_SSDD, RAW_TERAK_DSDD };
extern struct terak_media media[4];
extern volatile uint8_t *const sector;
extern uint8_t track_meta[512];
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
void qb_poll(void);
void qb_service(void);
#endif
