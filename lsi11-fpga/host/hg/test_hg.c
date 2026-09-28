#include "hg_directory.h"
#include "hg_image.h"
#include "hg_protocol.h"
#include "hg_time.h"

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

static void test_time(void)
{
	uint8_t header[HG_HEADER_SIZE] = {'H','G',1,HG_OP_TIME,0,50,0,6,0,0};
	uint8_t data[HG_TIME_SIZE];
	struct hg_request request;
	struct tm local = {.tm_year=126, .tm_mon=8, .tm_mday=22,
		                                            .tm_hour=23, .tm_min=59, .tm_sec=59
	};
	unsigned int hz, year, month, day;
	unsigned int cases = 0;

	header[9] = hg_header_checksum(header);
	assert(hg_decode_header(header, &request) == 0);
	for (size_t i = 0; i < HG_HEADER_SIZE - 1u; i++) {
		uint8_t save = header[i];
		header[i] ^= 0x80;
		assert(hg_decode_header(header, &request) != 0);
		header[9] = hg_header_checksum(header);
		assert(hg_decode_header(header, &request) != 0);
		header[i] = save;
		header[9] = hg_header_checksum(header);
	}
	for (hz = 50; hz <= 60; hz += 10) {
		header[5] = (uint8_t)hz;
		header[9] = hg_header_checksum(header);
		assert(hg_decode_header(header, &request) == 0);
		assert(hg_time_encode(&local, 999999999L, hz, data) == 0);
		assert((data[0] | data[1]<<8) == (1<<14 | 9<<10 | 22<<5 | 22));
		uint32_t ticks = ((uint32_t)(data[2] | data[3]<<8)<<16) |
		                 (uint32_t)(data[4] | data[5]<<8);
		assert(ticks == 86400u*hz-1);
		local.tm_hour=0;
		local.tm_min=0;
		local.tm_sec=0;
		assert(hg_time_encode(&local, 0, hz, data) == 0);
		assert(data[2]==0 && data[3]==0 && data[4]==0 && data[5]==0);
		local.tm_hour=23;
		local.tm_min=59;
		local.tm_sec=59;
	}
	for (year=1972; year<=2035; year++) {
		for (month=1; month<=12; month++) {
			for (day=1; day<=31; day++) {
				static const unsigned int days[]= {31,28,31,30,31,30,31,31,30,31,30,31};
				unsigned int limit=days[month-1]+(month==2 && year%4==0);
				local.tm_year=(int)year-1900;
				local.tm_mon=(int)month-1;
				local.tm_mday=(int)day;
				int result=hg_time_encode(&local,0,50,data);
				assert((result==0)==(day<=limit));
				if (result==0) {
					unsigned int date=data[0] | data[1]<<8;
					assert(1972+(date&31)+(date>>14)*32==year);
					assert(((date>>10)&15)==month && ((date>>5)&31)==day);
				}
				cases++;
			}
		}
	}
	local.tm_year=136;
	assert(hg_time_encode(&local,0,50,data)!=0);
	local.tm_year=71;
	assert(hg_time_encode(&local,0,50,data)!=0);
	local.tm_year=126;
	local.tm_mon=0;
	local.tm_mday=1;
	assert(hg_time_encode(&local,-1,50,data)!=0);
	assert(hg_time_encode(&local,1000000000L,50,data)!=0);
	assert(hg_time_encode(&local,0,51,data)!=0);
	local.tm_sec=60;
	assert(hg_time_encode(&local,0,50,data)!=0);
	printf("hg time: %u calendar cases, 50/60 Hz and malformed headers passed\n", cases);
}

static uint16_t test_word(const uint8_t *data, size_t word)
{
	return (uint16_t)(data[word * 2u] |
	                  ((uint16_t)data[word * 2u + 1u] << 8));
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
	uint8_t segment[RT11_BLOCK_SIZE * 2u];
	uint8_t recovered[5];
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
	fd = open(image_path, O_RDONLY);
	assert(fd >= 0);
	assert(lseek(fd, 6 * RT11_BLOCK_SIZE, SEEK_SET) ==
	       6 * RT11_BLOCK_SIZE);
	assert(read(fd, segment, sizeof(segment)) == (ssize_t)sizeof(segment));
	close(fd);
	assert(test_word(segment, 0) == 16);
	assert(test_word(segment, 1) == 0);
	assert(test_word(segment, 2) == 1);
	assert((test_word(segment, 5) & RT11_E_PERM) != 0);
	assert((test_word(segment, 12) & RT11_E_MPTY) != 0);
	assert(test_word(segment, 12 + 4) > 0);
	assert((test_word(segment, 19) & RT11_E_EOS) != 0);
	assert(test_word(segment, 19 + 4) == 0);
	assert(rt11_open_image(&image, image_path, "rb") == 0);
	assert(rt11_name_from_host("HELLO.TXT", &name) == 0);
	assert(rt11_find_file(&image, &name, &entry) == 0);
	rt11_close_image(&image);
	/* Simulate a guest write followed by a daemon crash before idle export.
	 * The stale host file still says hello; restart must preserve guest data. */
	fd = open(image_path, O_RDWR);
	assert(fd >= 0);
	assert(pwrite(fd, "guest", 5, (off_t)entry.start_block * RT11_BLOCK_SIZE) == 5);
	assert(fsync(fd) == 0);
	close(fd);
	assert(hg_directory_prepare(directory, image_path, 512) == 0);
	fd = open(image_path, O_RDONLY);
	assert(fd >= 0);
	assert(pread(fd, recovered, 5, (off_t)entry.start_block * RT11_BLOCK_SIZE) == 5);
	assert(memcmp(recovered, "guest", 5) == 0);
	assert(lseek(fd, 0, SEEK_END) == 256 * RT11_BLOCK_SIZE);
	close(fd);
	assert(hg_directory_export(directory, image_path) == 0);
	fd = open(exported, O_RDONLY);
	assert(fd >= 0 && read(fd, recovered, 5) == 5);
	assert(memcmp(recovered, "guest", 5) == 0);
	close(fd);
	/* Even an invalid existing mirror must not be silently reformatted. */
	fd = open(image_path, O_WRONLY | O_TRUNC);
	assert(fd >= 0 && write(fd, "broken", 6) == 6);
	close(fd);
	assert(hg_directory_prepare(directory, image_path, 256) != 0);
	fd = open(image_path, O_RDONLY);
	assert(fd >= 0 && lseek(fd, 0, SEEK_END) == 6);
	close(fd);
	unlink(source);
	unlink(exported);
	unlink(image_path);
	rmdir(directory);
}

int main(void)
{
	test_protocol();
	test_time();
	test_image();
	test_directory();
	puts("hg tests passed");
	return 0;
}
