#ifndef HG_PROTOCOL_H
#define HG_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define HG_BLOCK_SIZE 512u
#define HG_HEADER_SIZE 10u
#define HG_PROTOCOL_VERSION 1u

#define HG_OP_READ 1u
#define HG_OP_WRITE 2u
#define HG_OP_TIME 3u
#define HG_TIME_SIZE 6u
#define HG_OP_MASK 0x7fu
#define HG_OP_MORE 0x80u

enum hg_status {
	HG_STATUS_OK = 0,
	HG_STATUS_PROTOCOL = 1,
	HG_STATUS_RANGE = 2,
	HG_STATUS_IO = 3,
	HG_STATUS_CHECKSUM = 4,
	HG_STATUS_READ_ONLY = 5
};

struct hg_request {
	uint8_t operation;
	uint8_t more;
	uint8_t unit;
	uint16_t block;
	uint16_t count;
};

uint8_t hg_header_checksum(const uint8_t header[HG_HEADER_SIZE]);
int hg_decode_header(const uint8_t header[HG_HEADER_SIZE],
	struct hg_request *request);
uint16_t hg_data_checksum(const uint8_t *data, size_t length);

#endif
