#include "hg_directory.h"
#include "hg_image.h"
#include "hg_protocol.h"

#include "rt11fs.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_protocol(void)
{
	uint8_t header[HG_HEADER_SIZE] = {
		'H', 'G', HG_PROTOCOL_VERSION, HG_OP_WRITE | HG_OP_MORE,
		0, 0x34, 0x12, 0x00, 0x02, 0
	};
	struct hg_request request;
	uint8_t data[] = {1, 2, 3, 255};

	header[9] = hg_header_checksum(header);
	assert(hg_decode_header(header, &request) == 0);
	assert(request.operation == HG_OP_WRITE);
	assert(request.more == 1);
	assert(request.block == 0x1234);
	assert(request.count == 512);
	assert(hg_data_checksum(data, sizeof(data)) == 261);
	header[1] = 'X';
	assert(hg_decode_header(header, &request) != 0);
}

static void test_image(void)
{
	char path[] = "/tmp/hg-image.XXXXXX";
	uint8_t block[HG_BLOCK_SIZE * 2u];
	uint8_t data[] = {9, 8, 7, 6};
	uint8_t result[sizeof(data)];
	struct hg_image image = {.fd = -1};
	int fd = mkstemp(path);

	assert(fd >= 0);
	memset(block, 0xa5, sizeof(block));
	assert(write(fd, block, sizeof(block)) == (ssize_t)sizeof(block));
	close(fd);
	assert(hg_image_open(&image, path, 0) == 0);
	assert(hg_image_write(&image, 1, data, sizeof(data)) == 0);
	assert(hg_image_read(&image, 1, result, sizeof(result)) == 0);
	assert(memcmp(data, result, sizeof(data)) == 0);
	assert(hg_image_read(&image, 2, result, sizeof(result)) != 0);
	hg_image_close(&image);
	unlink(path);
}

static void test_directory(void)
{
	char directory[] = "/tmp/hg-directory.XXXXXX";
	char source[256];
	char exported[256];
	char image_path[256];
	rt11_image_t image;
	rt11_name_t name;
	rt11_dirent_t entry;
	int fd;

	assert(mkdtemp(directory) != NULL);
	snprintf(source, sizeof(source), "%s/hello.txt", directory);
	snprintf(exported, sizeof(exported), "%s/HELLO.TXT", directory);
	snprintf(image_path, sizeof(image_path), "%s/.hg-volume.dsk", directory);
	fd = open(source, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	assert(fd >= 0);
	assert(write(fd, "hello", 5) == 5);
	close(fd);
	assert(hg_directory_prepare(directory, image_path, 256) == 0);
	assert(rt11_open_image(&image, image_path, "rb") == 0);
	assert(rt11_name_from_host("HELLO.TXT", &name) == 0);
	assert(rt11_find_file(&image, &name, &entry) == 0);
	rt11_close_image(&image);
	assert(hg_directory_export(directory, image_path) == 0);
	unlink(source);
	unlink(exported);
	unlink(image_path);
	rmdir(directory);
}

int main(void)
{
	test_protocol();
	test_image();
	test_directory();
	puts("hg tests passed");
	return 0;
}
