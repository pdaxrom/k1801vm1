#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk_tape.h"

#define TAPE_RELAY_DELAY 10000
#define TAPE_SYNCH_DEFAULT 25

static unsigned tape_synch = TAPE_SYNCH_DEFAULT;
static int tape_synch_forced = 0;

static byte tape_read_val = 1;
static byte tape_write_val = 0;
static int tape_status = 1; /* 0 = moving, 1 = stopped */
static uint64_t tape_ticks = 0;
static uint64_t tape_read_ticks = 0;
static uint64_t tape_write_ticks = 0;

static byte *tape_in = NULL;
static size_t tape_in_size = 0;
static size_t tape_in_pos = 0;

static byte *tape_out = NULL;
static size_t tape_out_size = 0;
static size_t tape_out_cap = 0;
static int tape_out_enabled = 0;

static void tape_out_append(byte val, uint16_t duration)
{
	if (!tape_out_enabled) {
		return;
	}
	while (duration) {
		uint16_t chunk = (duration > 32767) ? 32767 : duration;
		byte b1 = (byte)((chunk >> 8) & 0x7f);
		b1 |= (byte)(val ? 0x80 : 0x00);
		byte b2 = (byte)(chunk & 0xff);
		if (tape_out_size + 2 > tape_out_cap) {
			size_t new_cap = tape_out_cap ? tape_out_cap * 2 : 256;
			byte *next = (byte *)realloc(tape_out, new_cap);
			if (!next) {
				return;
			}
			tape_out = next;
			tape_out_cap = new_cap;
		}
		tape_out[tape_out_size++] = b1;
		tape_out[tape_out_size++] = b2;
		duration -= chunk;
	}
}

static int tape_in_append(byte val, uint16_t duration, byte **buf, size_t *size, size_t *cap)
{
	while (duration) {
		uint16_t chunk = (duration > 32767) ? 32767 : duration;
		byte b1 = (byte)((chunk >> 8) & 0x7f);
		b1 |= (byte)(val ? 0x80 : 0x00);
		byte b2 = (byte)(chunk & 0xff);
		if (*size + 2 > *cap) {
			size_t new_cap = *cap ? (*cap * 2) : 256;
			byte *next = (byte *)realloc(*buf, new_cap);
			if (!next) {
				return -1;
			}
			*buf = next;
			*cap = new_cap;
		}
		(*buf)[(*size)++] = b1;
		(*buf)[(*size)++] = b2;
		duration -= chunk;
	}
	return 0;
}

static int tape_put_si(byte **buf, size_t *size, size_t *cap)
{
	return tape_in_append(1, (uint16_t)tape_synch, buf, size, cap) ||
	       tape_in_append(0, (uint16_t)tape_synch, buf, size, cap);
}

static int tape_put0(byte **buf, size_t *size, size_t *cap)
{
	if (tape_in_append(1, (uint16_t)tape_synch, buf, size, cap) != 0) {
		return -1;
	}
	if (tape_in_append(0, (uint16_t)tape_synch, buf, size, cap) != 0) {
		return -1;
	}
	return tape_put_si(buf, size, cap);
}

static int tape_put1(byte **buf, size_t *size, size_t *cap)
{
	if (tape_in_append(1, (uint16_t)(2 * tape_synch), buf, size, cap) != 0) {
		return -1;
	}
	if (tape_in_append(0, (uint16_t)(2 * tape_synch), buf, size, cap) != 0) {
		return -1;
	}
	return tape_put_si(buf, size, cap);
}

static int tape_put_inm(byte **buf, size_t *size, size_t *cap)
{
	return tape_in_append(1, (uint16_t)(8 * tape_synch), buf, size, cap);
}

static int tape_put_outm(byte **buf, size_t *size, size_t *cap)
{
	if (tape_in_append(1, (uint16_t)(4 * tape_synch), buf, size, cap) != 0) {
		return -1;
	}
	return tape_in_append(0, (uint16_t)(4 * tape_synch), buf, size, cap);
}

static int tape_put_tune(unsigned int count, byte **buf, size_t *size, size_t *cap)
{
	while (count--) {
		if (tape_put_si(buf, size, cap) != 0) {
			return -1;
		}
	}
	return 0;
}

static int tape_put_byte(byte b, byte **buf, size_t *size, size_t *cap)
{
	for (int i = 0; i < 8; i++) {
		if (b & 1) {
			if (tape_put1(buf, size, cap) != 0) {
				return -1;
			}
		} else {
			if (tape_put0(buf, size, cap) != 0) {
				return -1;
			}
		}
		b >>= 1;
	}
	return 0;
}

static int tape_put_array(const byte *p, size_t len, byte **buf, size_t *size, size_t *cap)
{
	for (size_t i = 0; i < len; i++) {
		if (tape_put_byte(p[i], buf, size, cap) != 0) {
			return -1;
		}
	}
	return 0;
}

static word tape_checksum(const byte *data, word len)
{
	uint32_t sum = 0;
	for (word i = 0; i < len; i++) {
		sum += data[i];
		if (sum & 0200000) {
			sum = (sum & 0xFFFF) + 1;
		}
	}
	return (word)sum;
}

typedef struct {
	const byte *data;
	size_t size;
	size_t pos;
	uint16_t old_strobe;
} tape_raw_reader;

static int tape_raw_next_word(tape_raw_reader *r, uint16_t *out)
{
	if (r->pos + 1 >= r->size) {
		return -1;
	}
	uint16_t v = (uint16_t)(r->data[r->pos] << 8);
	v |= r->data[r->pos + 1];
	r->pos += 2;
	*out = v;
	return 0;
}

static int tape_raw_get_strobe(tape_raw_reader *r, int *len, int *val)
{
	uint16_t c;
again:
	if (tape_raw_next_word(r, &c) != 0) {
		return -1;
	}
	if ((c & 0x7fff) > 1) {
		uint16_t toret = r->old_strobe;
		r->old_strobe = c;
		*len = toret & 0x7fff;
		*val = (toret >> 15) & 1;
		return 0;
	}
	r->old_strobe++;
	if (tape_raw_next_word(r, &c) != 0) {
		return -1;
	}
	r->old_strobe = (uint16_t)(r->old_strobe + (c & 0x7fff));
	goto again;
}

static int tape_read_bit_raw(tape_raw_reader *r, int polarity)
{
	int len1, len2, len3, len4, val;
	do {
		if (tape_raw_get_strobe(r, &len1, &val) != 0) {
			return -1;
		}
	} while (val != polarity);
	if (tape_raw_get_strobe(r, &len2, &val) != 0) return -1;
	if (tape_raw_get_strobe(r, &len3, &val) != 0) return -1;
	if (tape_raw_get_strobe(r, &len4, &val) != 0) return -1;
	return (len1 > len3 ? len1 : len3) + (len2 > len4 ? len2 : len4);
}

static int tape_find_header(tape_raw_reader *r, int *polarity, int *threshold)
{
	enum { SENSE = 150 };
	int strobe_cnt = 0;
	int strobe_len = 0;
	int len = 0;
	int val = 0;
	for (;;) {
		if (tape_raw_get_strobe(r, &len, &val) != 0) {
			return -1;
		}
		if (strobe_len - len > SENSE) {
			strobe_cnt = 0;
			strobe_len = 0;
		} else {
			strobe_len = len;
			strobe_cnt++;
		}
		if (strobe_cnt > 04000) {
			break;
		}
	}
	int sum = 0;
	for (int i = 0; i < 0200; i++) {
		int bit = tape_read_bit_raw(r, val);
		if (bit < 0) {
			return -1;
		}
		sum += bit;
	}
	sum >>= 7;
	*threshold = sum + (sum >> 1);
	int num = 0;
	do {
		if (tape_raw_get_strobe(r, &len, &val) != 0) {
			return -1;
		}
		num++;
	} while (len < *threshold);
	if (len > 2 * (*threshold) || num < 100) {
		return -1;
	}
	*polarity = val;
	(void)tape_read_bit_raw(r, *polarity);
	return 0;
}

static int tape_read_array_raw(tape_raw_reader *r, int polarity, int threshold,
                               byte *out, size_t len)
{
	int l, val;
	do {
		do {
			if (tape_raw_get_strobe(r, &l, &val) != 0) {
				return -1;
			}
		} while (val != polarity);
	} while (l < threshold);
	if (l > threshold * 2) {
		return -1;
	}
	if (tape_read_bit_raw(r, polarity) < 0) {
		return -1;
	}

	for (size_t i = 0; i < len; i++) {
		byte b = 0;
		for (int bit = 0; bit < 8; bit++) {
			int v = tape_read_bit_raw(r, polarity);
			if (v < 0) {
				return -1;
			}
			if (v > threshold) {
				b |= (byte)(1 << bit);
			}
		}
		out[i] = b;
	}
	return 0;
}

int bk_tape_decode_raw_to_bin(const byte *raw, size_t raw_size,
                              byte **out_data, size_t *out_size,
                              char *name_out, size_t name_size,
                              byte *name_raw, size_t name_raw_size)
{
    if (!raw || raw_size < 4 || !out_data || !out_size) {
        return -1;
    }
	tape_raw_reader r = { raw, raw_size, 0, 0 };
    int polarity = 0;
    int threshold = 0;
    if (tape_find_header(&r, &polarity, &threshold) != 0) {
        return -1;
    }
    byte header[024];
    if (tape_read_array_raw(&r, polarity, threshold, header, sizeof(header)) != 0) {
        return -1;
    }
    word addr = (word)(header[1] << 8 | header[0]);
    word len = (word)(header[3] << 8 | header[2]);
    if (name_out && name_size) {
        size_t n = (name_size - 1 < 16) ? name_size - 1 : 16;
        memcpy(name_out, &header[4], n);
        name_out[n] = '\0';
        for (size_t i = n; i > 0; i--) {
            if (name_out[i - 1] == ' ' || name_out[i - 1] == '\0') {
                name_out[i - 1] = '\0';
            } else {
                break;
            }
        }
    }
    if (name_raw && name_raw_size) {
        size_t n = name_raw_size > 16 ? 16 : name_raw_size;
        memcpy(name_raw, &header[4], n);
        if (n < name_raw_size) {
            memset(name_raw + n, 0, name_raw_size - n);
        }
    }
    byte *buf = (byte *)malloc((size_t)len + 4);
    if (!buf) {
        return -1;
    }
	buf[0] = (byte)(addr & 0xff);
	buf[1] = (byte)(addr >> 8);
	buf[2] = (byte)(len & 0xff);
	buf[3] = (byte)(len >> 8);
    if (tape_read_array_raw(&r, polarity, threshold, buf + 4, len) != 0) {
        free(buf);
        return -1;
    }
    word checksum = 0;
    for (int i = 0; i < 16; i++) {
        int v = tape_read_bit_raw(&r, polarity);
        if (v < 0) {
            free(buf);
            return -1;
        }
        if (v > threshold) {
            checksum |= (word)(1u << i);
        }
    }
    word sum = tape_checksum(buf + 4, len);
    if (checksum != sum) {
        free(buf);
        return -1;
    }
	*out_data = buf;
	*out_size = (size_t)len + 4;
	return 0;
}

void bk_tape_init(void)
{
	const char *synch_env = getenv("BK_TAPE_SYNCH");
	if (synch_env && *synch_env) {
		long v = strtol(synch_env, NULL, 0);
		if (v > 0 && v <= 100000) {
			tape_synch = (unsigned)v;
			tape_synch_forced = 1;
		}
	}
}

void bk_tape_reset(void)
{
	tape_ticks = 0;
	tape_read_ticks = 0;
	tape_write_ticks = 0;
	tape_read_val = 1;
	tape_write_val = 0;
	tape_status = 1;
	tape_in_pos = 0;
}

void bk_tape_set_tick_hz(unsigned int hz)
{
	if (tape_synch_forced) {
		return;
	}
	if (hz == 0) {
		return;
	}
	unsigned sync = hz / 3000u;
	if (sync == 0) {
		sync = 1;
	}
	tape_synch = sync;
}

void bk_tape_tick(void)
{
	tape_ticks++;
}

void bk_tape_write(int motor_on, int val)
{
	int status = motor_on ? 0 : 1;
	if (status != tape_status) {
		tape_ticks += TAPE_RELAY_DELAY;
		tape_status = status;
		if (!tape_status) {
			tape_read_ticks = tape_write_ticks = tape_ticks;
		} else if (tape_out_enabled) {
			tape_out_append(tape_write_val, (uint16_t)(tape_ticks - tape_write_ticks));
			tape_write_ticks = tape_ticks;
		}
	}
	if (!tape_status && val != tape_write_val) {
		uint64_t delta = tape_ticks - tape_write_ticks;
		if (delta) {
			tape_out_append(tape_write_val, (uint16_t)delta);
		}
		tape_write_ticks = tape_ticks;
		tape_write_val = (byte)(val ? 1 : 0);
	}
}

int bk_tape_read(void)
{
	if (tape_status || !tape_in || tape_in_pos + 1 >= tape_in_size) {
		tape_read_val = (byte)!tape_read_val;
		return tape_read_val;
	}
	while (tape_in_pos + 1 < tape_in_size && tape_ticks > tape_read_ticks) {
		byte c1 = tape_in[tape_in_pos++];
		byte c2 = tape_in[tape_in_pos++];
		uint16_t delta = (uint16_t)((c1 << 8) | c2);
		tape_read_val = (byte)((delta >> 15) & 1);
		delta &= 0x7fff;
		tape_read_ticks += delta;
	}
	return tape_read_val;
}

int bk_tape_set_input(const byte *data, size_t size)
{
	if (tape_in) {
		free(tape_in);
		tape_in = NULL;
	}
	if (!data || size == 0) {
		tape_in_size = 0;
		tape_in_pos = 0;
		return -1;
	}
	tape_in = (byte *)malloc(size);
	if (!tape_in) {
		return -1;
	}
	memcpy(tape_in, data, size);
	tape_in_size = size;
	tape_in_pos = 0;
	return 0;
}

static int tape_encode_bin_to_raw_internal(const byte *data, size_t size,
                                           const byte *name_raw, size_t name_raw_size,
                                           const char *name,
                                           byte **out_raw, size_t *out_size)
{
	if (!data || size < 4) {
		return -1;
	}
	word addr = (word)(data[0] | (data[1] << 8));
	word len = (word)(data[2] | (data[3] << 8));

	if ((size_t)len > size - 4) {
		return -1;
	}

	byte *buf = NULL;
	size_t out_size_local = 0;
	size_t out_cap = 0;

	if (tape_put_inm(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_tune(010000, &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_outm(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put1(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_tune(010, &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_outm(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put1(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	byte header[16];
	memset(header, 0, sizeof(header));
	if (name_raw && name_raw_size) {
		size_t nlen = name_raw_size > 16 ? 16 : name_raw_size;
		memcpy(header, name_raw, nlen);
	} else if (name) {
		memset(header, ' ', sizeof(header));
		size_t nlen = strlen(name);
		if (nlen > 16) {
			nlen = 16;
		}
		memcpy(header, name, nlen);
	}

	if (tape_put_byte((byte)(addr & 0xff), &buf, &out_size_local, &out_cap) != 0 ||
	    tape_put_byte((byte)(addr >> 8), &buf, &out_size_local, &out_cap) != 0 ||
	    tape_put_byte((byte)(len & 0xff), &buf, &out_size_local, &out_cap) != 0 ||
	    tape_put_byte((byte)(len >> 8), &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	if (tape_put_array(header, sizeof(header), &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	if (tape_put_tune(010, &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_outm(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put1(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	const byte *payload = data + 4;
	if (tape_put_array(payload, len, &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	word sum = tape_checksum(payload, len);
	if (tape_put_byte((byte)(sum & 0xff), &buf, &out_size_local, &out_cap) != 0 ||
	    tape_put_byte((byte)(sum >> 8), &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	if (tape_put_inm(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_tune(0400, &buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put_outm(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}
	if (tape_put1(&buf, &out_size_local, &out_cap) != 0) {
		goto fail;
	}

	if (out_raw) {
		*out_raw = buf;
	}
	if (out_size) {
		*out_size = out_size_local;
	}
	return 0;

fail:
	free(buf);
	return -1;
}

int bk_tape_encode_bin_to_raw(const byte *data, size_t size, const char *name,
                              byte **out_raw, size_t *out_size)
{
	return tape_encode_bin_to_raw_internal(data, size, NULL, 0, name, out_raw, out_size);
}

int bk_tape_set_input_bin(const byte *data, size_t size, const char *name)
{
	byte *raw = NULL;
	size_t raw_size = 0;
	if (bk_tape_encode_bin_to_raw(data, size, name, &raw, &raw_size) != 0) {
		return -1;
	}
	int rc = bk_tape_set_input(raw, raw_size);
	free(raw);
	return rc;
}
void bk_tape_set_output_enabled(int enable)
{
	tape_out_enabled = enable ? 1 : 0;
	if (!tape_out_enabled) {
		return;
	}
	tape_out_size = 0;
	tape_out_cap = 0;
	if (tape_out) {
		free(tape_out);
		tape_out = NULL;
	}
}

const byte *bk_tape_output_data(size_t *size)
{
	if (tape_out_enabled && !tape_status) {
		uint64_t delta = tape_ticks - tape_write_ticks;
		if (delta) {
			tape_out_append(tape_write_val, (uint16_t)delta);
			tape_write_ticks = tape_ticks;
		}
	}
	if (size) {
		*size = tape_out_size;
	}
	return tape_out;
}

void bk_tape_output_clear(void)
{
	if (tape_out) {
		free(tape_out);
		tape_out = NULL;
	}
	tape_out_size = 0;
	tape_out_cap = 0;
}

void bk_tape_rewind(void)
{
	tape_in_pos = 0;
	tape_read_ticks = tape_ticks;
}
