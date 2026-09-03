#include "hg_directory.h"
#include "hg_image.h"
#include "hg_mpsse.h"
#include "hg_protocol.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static volatile sig_atomic_t hg_stop;

static void hg_signal(int signal_number)
{
	(void)signal_number;
	hg_stop = 1;
}

static void hg_delay_us(unsigned int microseconds)
{
	struct timespec delay;

	delay.tv_sec = microseconds / 1000000u;
	delay.tv_nsec = (long)(microseconds % 1000000u) * 1000L;
	nanosleep(&delay, NULL);
}

static uint64_t hg_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int hg_send_status(struct hg_mpsse *link, enum hg_status status)
{
	uint8_t byte = (uint8_t)status;

	hg_delay_us(500);
	return hg_mpsse_exchange(link, &byte, NULL, 1);
}

static enum hg_status hg_status_from_errno(int error, int writing)
{
	if (error == ERANGE)
		return HG_STATUS_RANGE;
	if (writing && error == EROFS)
		return HG_STATUS_READ_ONLY;
	return HG_STATUS_IO;
}

/* Return 1 after a successful write, 0 otherwise, and -1 on link failure. */
static int hg_serve_one(struct hg_mpsse *link, struct hg_image *image,
	struct hg_request *request_out)
{
	uint8_t header[HG_HEADER_SIZE];
	uint8_t payload[HG_BLOCK_SIZE + 2u];
	struct hg_request request;
	uint16_t checksum;
	enum hg_status status;
	int wrote = 0;

	if (hg_mpsse_select(link, 1) != 0)
		return -1;
	hg_delay_us(2000);
	if (hg_mpsse_exchange(link, NULL, header, sizeof(header)) != 0)
		goto link_error;
	if (hg_decode_header(header, &request) != 0) {
		size_t i;

		fprintf(stderr, "hgfsd: rejected malformed request:");
		for (i = 0; i < sizeof(header); i++)
			fprintf(stderr, " %02x", header[i]);
		fputc('\n', stderr);
		if (hg_send_status(link, HG_STATUS_PROTOCOL) != 0)
			goto link_error;
		goto done;
	}
	if (request_out)
		*request_out = request;

	if (request.operation == HG_OP_READ) {
		if (hg_image_read(image, request.block, payload, request.count) != 0) {
			status = hg_status_from_errno(errno, 0);
			if (hg_send_status(link, status) != 0)
				goto link_error;
			goto done;
		}
		if (hg_send_status(link, HG_STATUS_OK) != 0)
			goto link_error;
		checksum = hg_data_checksum(payload, request.count);
		payload[request.count] = (uint8_t)(checksum & 0xffu);
		payload[request.count + 1u] = (uint8_t)(checksum >> 8);
		hg_delay_us(500);
		if (hg_mpsse_exchange(link, payload, NULL, request.count + 2u) != 0)
			goto link_error;
	} else {
		if (image->read_only) {
			if (hg_send_status(link, HG_STATUS_READ_ONLY) != 0)
				goto link_error;
			goto done;
		}
		if ((uint64_t)request.block * HG_BLOCK_SIZE + request.count >
		    image->size) {
			if (hg_send_status(link, HG_STATUS_RANGE) != 0)
				goto link_error;
			goto done;
		}
		if (hg_send_status(link, HG_STATUS_OK) != 0)
			goto link_error;
		hg_delay_us(500);
		if (hg_mpsse_exchange(link, NULL, payload, request.count + 2u) != 0)
			goto link_error;
		checksum = hg_data_checksum(payload, request.count);
		if (payload[request.count] != (uint8_t)(checksum & 0xffu) ||
		    payload[request.count + 1u] != (uint8_t)(checksum >> 8)) {
			status = HG_STATUS_CHECKSUM;
		} else if (hg_image_write(image, request.block, payload,
			request.count) != 0) {
			status = hg_status_from_errno(errno, 1);
		} else {
			status = HG_STATUS_OK;
			wrote = 1;
		}
		if (hg_send_status(link, status) != 0)
			goto link_error;
	}

done:
	hg_delay_us(500);
	if (hg_mpsse_select(link, 0) != 0)
		return -1;
	return wrote;

link_error:
	hg_mpsse_select(link, 0);
	return -1;
}

static void hg_usage(FILE *out)
{
	fprintf(out,
		"usage: hgfsd (--image FILE | --directory DIR) [options]\n"
		"  --read-only       reject RT-11 writes\n"
		"  --blocks N        directory image size (default 8192)\n"
		"  --clock HZ        MPSSE clock (default 4000)\n"
		"  --serial TEXT     select an FT2232 by serial number\n"
		"  --index N         select matching FT2232 index (default 0)\n"
		"  --vid N --pid N   USB ids (defaults 0x0403:0x6010)\n");
}

static unsigned long hg_number(const char *text, const char *option)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 0);
	if (errno || !text[0] || *end) {
		fprintf(stderr, "hgfsd: invalid %s value: %s\n", option, text);
		exit(2);
	}
	return value;
}

int main(int argc, char **argv)
{
	const char *image_path = NULL;
	const char *directory = NULL;
	const char *serial = NULL;
	char directory_image[PATH_MAX];
	unsigned int blocks = 8192;
	unsigned int clock_hz = 4000;
	unsigned int index = 0;
	int vendor = 0x0403;
	int product = 0x6010;
	int read_only = 0;
	struct hg_image image = {.fd = -1};
	struct hg_mpsse link;
	uint64_t last_write = 0;
	int mirror_dirty = 0;
	int i;
	int result = 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--image") == 0 && i + 1 < argc)
			image_path = argv[++i];
		else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
			directory = argv[++i];
		else if (strcmp(argv[i], "--read-only") == 0)
			read_only = 1;
		else if (strcmp(argv[i], "--blocks") == 0 && i + 1 < argc)
			blocks = (unsigned int)hg_number(argv[++i], "--blocks");
		else if (strcmp(argv[i], "--clock") == 0 && i + 1 < argc)
			clock_hz = (unsigned int)hg_number(argv[++i], "--clock");
		else if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc)
			serial = argv[++i];
		else if (strcmp(argv[i], "--index") == 0 && i + 1 < argc)
			index = (unsigned int)hg_number(argv[++i], "--index");
		else if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc)
			vendor = (int)hg_number(argv[++i], "--vid");
		else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc)
			product = (int)hg_number(argv[++i], "--pid");
		else if (strcmp(argv[i], "--help") == 0) {
			hg_usage(stdout);
			return 0;
		} else {
			hg_usage(stderr);
			return 2;
		}
	}
	if ((!image_path && !directory) || (image_path && directory)) {
		hg_usage(stderr);
		return 2;
	}
	if (directory) {
		int length = snprintf(directory_image, sizeof(directory_image),
			"%s/.hg-volume.dsk", directory);
		if (length < 0 || (size_t)length >= sizeof(directory_image)) {
			fprintf(stderr, "hgfsd: directory path is too long\n");
			return 1;
		}
		image_path = directory_image;
		if (hg_directory_prepare(directory, image_path, blocks) != 0) {
			fprintf(stderr, "hgfsd: cannot prepare %s: %s\n", image_path,
				strerror(errno));
			return 1;
		}
	}
	if (hg_image_open(&image, image_path, read_only) != 0) {
		fprintf(stderr, "hgfsd: cannot open %s: %s\n", image_path,
			strerror(errno));
		return 1;
	}
	if (hg_mpsse_open(&link, vendor, product, serial, index, clock_hz) != 0) {
		fprintf(stderr, "hgfsd: cannot open FT2232 channel A: %s\n",
			strerror(errno));
		goto out_image;
	}

	signal(SIGINT, hg_signal);
	signal(SIGTERM, hg_signal);
	fprintf(stderr, "hgfsd: serving %s at %u Hz%s\n", image_path,
		link.clock_hz, read_only ? " read-only" : "");
	while (!hg_stop) {
		int pending = hg_mpsse_request_pending(&link);
		if (pending < 0) {
			fprintf(stderr, "hgfsd: FT2232 read failed\n");
			goto out_link;
		}
		if (pending) {
			struct hg_request request = {0};
			int served = hg_serve_one(&link, &image, &request);
			if (served < 0) {
				fprintf(stderr, "hgfsd: link transaction failed\n");
				goto out_link;
			}
			if (served) {
				mirror_dirty = directory != NULL;
				last_write = hg_milliseconds();
				fsync(image.fd);
			}
			if (request.operation != 0)
				fprintf(stderr, "hgfsd: %s block %u, %u bytes%s\n",
					request.operation == HG_OP_WRITE ? "write" : "read",
					request.block, request.count,
					request.more ? " (more)" : "");
			continue;
		}
		if (mirror_dirty && hg_milliseconds() - last_write >= 500u) {
			if (hg_directory_export(directory, image_path) == 0)
				mirror_dirty = 0;
		}
		hg_delay_us(2000);
	}
	result = 0;

out_link:
	hg_mpsse_close(&link);
	if (mirror_dirty)
		hg_directory_export(directory, image_path);
out_image:
	hg_image_close(&image);
	return result;
}
