/* The compact RL buffer must preserve the reserved on-disk TRK1 tail. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/terak/terak.h"

static uint8_t buffer[512], original[512];
volatile uint8_t *const sector = buffer;
struct terak_media rl_media[4];
static unsigned writes;

int fetch(u32 lba)
{
	assert(lba == 101);
	memcpy(buffer, original, 512);
	return 1;
}

int store(u32 lba)
{
	assert(lba == 101);
	assert(!memcmp(buffer + 288, original + 288, 220));
	assert(media_crc(buffer) == word(508));
	memcpy(original, buffer, 512);
	writes++;
	return 1;
}

int mbr_mount(u32 capacity)
{
	(void)capacity;
	assert(0);
	return 0;
}

int main(void)
{
	memcpy(buffer, "TRK1", 4);
	buffer[4] = 32;
	for (unsigned i = 32; i < 288; i++) {
		buffer[i] = i;
	}
	for (unsigned i = 288; i < 508; i++) {
		buffer[i] = i ^ 0x5a;
	}
	u32 crc = media_crc(buffer);
	for (unsigned i = 0; i < 4; i++) {
		buffer[508 + i] = crc >> (i * 8);
	}
	memcpy(original, buffer, 512);
	media[0] = (struct terak_media) {
		100, 2542, 1, 0, MEDIA_FD
	};
	assert(sizeof(track_meta) == 288 && track_load(0, 0, 0));
	assert(track_meta[287] == (uint8_t)287);
	track_meta[36] = 3;
	track_meta[284] = 2;
	assert(track_save(0, 0, 0) && writes == 1);
	assert(track_load(0, 0, 0) && track_meta[36] == 3 && track_meta[284] == 2);
	media[0].readonly = 1;
	assert(!track_save(0, 0, 0) && writes == 1);
	puts("PASS all 32 TRK1 entries, reserved tail, CRC and read-only guard");
	return 0;
}
