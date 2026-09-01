#include "hg_image.h"
#include "hg_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int hg_image_range(const struct hg_image *image, uint16_t block,
	size_t length, off_t *offset)
{
	uint64_t start = (uint64_t)block * HG_BLOCK_SIZE;

	if (!image || image->fd < 0 || length == 0 || length > HG_BLOCK_SIZE ||
	    start > image->size || length > image->size - start) {
		errno = ERANGE;
		return -1;
	}
	*offset = (off_t)start;
	return 0;
}

int hg_image_open(struct hg_image *image, const char *path, int read_only)
{
	struct stat st;
	int flags = read_only ? O_RDONLY : O_RDWR;

	if (!image || !path) {
		errno = EINVAL;
		return -1;
	}
	image->fd = open(path, flags);
	if (image->fd < 0)
		return -1;
	if (fstat(image->fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size <= 0 || (st.st_size % HG_BLOCK_SIZE) != 0) {
		int saved = errno ? errno : EINVAL;
		close(image->fd);
		image->fd = -1;
		errno = saved;
		return -1;
	}
	image->size = (uint64_t)st.st_size;
	image->read_only = read_only;
	return 0;
}

void hg_image_close(struct hg_image *image)
{
	if (!image)
		return;
	if (image->fd >= 0)
		close(image->fd);
	image->fd = -1;
	image->size = 0;
}

int hg_image_read(struct hg_image *image, uint16_t block, uint8_t *data,
	size_t length)
{
	off_t offset;
	ssize_t done;

	if (!data || hg_image_range(image, block, length, &offset) != 0)
		return -1;
	done = pread(image->fd, data, length, offset);
	if (done != (ssize_t)length) {
		if (done >= 0)
			errno = EIO;
		return -1;
	}
	return 0;
}

int hg_image_write(struct hg_image *image, uint16_t block,
	const uint8_t *data, size_t length)
{
	off_t offset;
	ssize_t done;

	if (!data || hg_image_range(image, block, length, &offset) != 0)
		return -1;
	if (image->read_only) {
		errno = EROFS;
		return -1;
	}
	done = pwrite(image->fd, data, length, offset);
	if (done != (ssize_t)length) {
		if (done >= 0)
			errno = EIO;
		return -1;
	}
	return 0;
}
