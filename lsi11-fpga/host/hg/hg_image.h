#ifndef HG_IMAGE_H
#define HG_IMAGE_H

#include <stddef.h>
#include <stdint.h>

struct hg_image {
	int fd;
	uint64_t size;
	int read_only;
};

int hg_image_open(struct hg_image *image, const char *path, int read_only);
void hg_image_close(struct hg_image *image);
int hg_image_read(struct hg_image *image, uint16_t block, uint8_t *data,
	size_t length);
int hg_image_write(struct hg_image *image, uint16_t block,
	const uint8_t *data, size_t length);

#endif
