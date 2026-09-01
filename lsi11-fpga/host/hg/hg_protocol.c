#include "hg_protocol.h"

#include <errno.h>

uint8_t hg_header_checksum(const uint8_t header[HG_HEADER_SIZE])
{
	uint8_t checksum = 0;
	size_t i;

	for (i = 0; i < HG_HEADER_SIZE - 1u; i++)
		checksum ^= header[i];
	return checksum;
}

int hg_decode_header(const uint8_t header[HG_HEADER_SIZE],
	struct hg_request *request)
{
	uint8_t operation;

	if (!header || !request) {
		errno = EINVAL;
		return -1;
	}
	operation = header[3] & HG_OP_MASK;
	if (header[0] != 'H' || header[1] != 'G' ||
	    header[2] != HG_PROTOCOL_VERSION ||
	    (operation != HG_OP_READ && operation != HG_OP_WRITE) ||
	    hg_header_checksum(header) != header[HG_HEADER_SIZE - 1u]) {
		errno = EPROTO;
		return -1;
	}

	request->operation = operation;
	request->more = (header[3] & HG_OP_MORE) != 0;
	request->unit = header[4];
	request->block = (uint16_t)(header[5] | ((uint16_t)header[6] << 8));
	request->count = (uint16_t)(header[7] | ((uint16_t)header[8] << 8));
	if (request->unit != 0 || request->count == 0 ||
	    request->count > HG_BLOCK_SIZE) {
		errno = EPROTO;
		return -1;
	}
	return 0;
}

uint16_t hg_data_checksum(const uint8_t *data, size_t length)
{
	uint16_t checksum = 0;
	size_t i;

	for (i = 0; i < length; i++)
		checksum = (uint16_t)(checksum + data[i]);
	return checksum;
}
