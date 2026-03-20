#include "rsx11fs.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define RSX11_HOME_BLOCK 1u
#define RSX11_HOME_OFF_IBSZ 0u
#define RSX11_HOME_OFF_IBLB 2u
#define RSX11_HOME_OFF_FMAX 6u
#define RSX11_HOME_OFF_SBCL 8u
#define RSX11_HOME_OFF_DVTY 10u
#define RSX11_HOME_OFF_VLEV 12u
#define RSX11_HOME_OFF_VNAM 14u
#define RSX11_HOME_OFF_VOWN 30u
#define RSX11_HOME_OFF_VPRO 32u
#define RSX11_HOME_OFF_VCHA 34u
#define RSX11_HOME_OFF_DFPR 36u
#define RSX11_HOME_OFF_WISZ 44u
#define RSX11_HOME_OFF_FIEX 45u
#define RSX11_HOME_OFF_LRUC 46u
#define RSX11_HOME_OFF_REVD 48u
#define RSX11_HOME_OFF_CHK1 58u
#define RSX11_HOME_OFF_VDAT 60u
#define RSX11_HOME_OFF_INDN 472u
#define RSX11_HOME_OFF_INDO 484u
#define RSX11_HOME_OFF_INDF 496u
#define RSX11_HOME_OFF_CHK2 510u

#define RSX11_HEADER_MIN 128u
#define RSX11_MAX_EXTENTS 1024u
#define RSX11_MAX_HEADER_SEGMENTS 64u
#define RSX11_MAP_MAX_WORDS 0xccu
#define RSX11_HDR_OFF_UFAT 14u
#define RSX11_FCS_OFF_RATT 0u
#define RSX11_FCS_OFF_RTYP 1u
#define RSX11_FCS_OFF_RSIZ 2u
#define RSX11_FCS_OFF_HIBK 4u
#define RSX11_FCS_OFF_EFBK 8u
#define RSX11_FCS_OFF_FFBY 12u

typedef struct {
    uint32_t lbn;
    uint16_t blocks;
} rsx11_extent_t;

typedef struct {
    uint64_t raw_offset;
    rsx11_fid_t fid;
    uint8_t segment_number;
    rsx11_fid_t next_fid;
    size_t header_count;
    rsx11_fid_t header_fids[RSX11_MAX_HEADER_SEGMENTS];
    char name[10];
    char ext[4];
    uint16_t version;
    uint64_t size_bytes;
    size_t extent_count;
    rsx11_extent_t extents[RSX11_MAX_EXTENTS];
} rsx11_file_header_t;

typedef struct {
    rsx11_fid_t fid;
    char name[10];
    char ext[4];
    uint16_t version;
} rsx11_dir_record_t;

typedef struct {
    const char *name;
    const char *ext;
    rsx11_fid_t fid;
    uint32_t start_lbn;
    uint32_t blocks;
} rsx11_mkfs_file_t;

typedef struct {
    rsx11_home_t home;
    int have_home;
    rsx11_file_header_t *items;
    size_t count;
    size_t cap;
} rsx11_header_cache_t;

typedef int (*rsx11_dir_record_cb_t)(const uint8_t *rec, uint64_t raw_offset,
                                     void *opaque);

static int read_header_slot(rsx11_image_t *img, const rsx11_home_t *home,
                            uint16_t fnum, rsx11_file_header_t *out);
static uint32_t header_total_blocks(const rsx11_file_header_t *hdr);
static uint16_t get_le16(const uint8_t *p);
static int read_at(FILE *fp, uint64_t off, void *buf, size_t len);
static void trim_right(char *s);
static void rad50_decode_word(uint16_t word, char out[4]);
static void rad50_decode_name3(const uint8_t *p, char *out, size_t out_size);
static void fsck_issue(FILE *out, rsx11_fsck_report_t *report,
                       const rsx11_dirent_t *ent, const char *msg);
static void fsck_fatal(FILE *out, rsx11_fsck_report_t *report,
                       const rsx11_dirent_t *ent, const char *msg);
static int fsck_clear_dir_record(rsx11_image_t *img, const rsx11_dirent_t *ent);

static int dir_record_is_empty(const uint8_t *rec)
{
    size_t i;

    for (i = 0; i < 16u; i++) {
        if (rec[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static void decode_dir_record_loose(const uint8_t *rec, rsx11_dir_record_t *out)
{
    memset(out, 0, sizeof(*out));
    out->fid.num = get_le16(rec + 0u);
    out->fid.seq = get_le16(rec + 2u);
    out->fid.rvn = get_le16(rec + 4u);
    rad50_decode_name3(rec + 6u, out->name, sizeof(out->name));
    rad50_decode_word(get_le16(rec + 12u), out->ext);
    trim_right(out->ext);
    out->version = get_le16(rec + 14u);
}

static int walk_dir_records(rsx11_image_t *img, const rsx11_file_header_t *hdr,
                            rsx11_dir_record_cb_t cb, void *opaque)
{
    uint8_t block[RSX11_BLOCK_SIZE];
    uint64_t remaining_bytes = hdr->size_bytes;
    size_t i;
    uint32_t b;

    if (remaining_bytes == 0u) {
        remaining_bytes = (uint64_t)header_total_blocks(hdr) * RSX11_BLOCK_SIZE;
    }

    for (i = 0; i < hdr->extent_count; i++) {
        uint32_t lbn = hdr->extents[i].lbn;
        uint32_t count = hdr->extents[i].blocks;

        for (b = 0; b < count; b++) {
            size_t rec_off;
            size_t block_bytes;

            if (remaining_bytes == 0u) {
                break;
            }
            if ((uint64_t)(lbn + b + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
                errno = EIO;
                return -1;
            }
            if (read_at(img->fp, (uint64_t)(lbn + b) * RSX11_BLOCK_SIZE,
                        block, sizeof(block)) != 0) {
                return -1;
            }

            block_bytes = remaining_bytes >= RSX11_BLOCK_SIZE
                              ? RSX11_BLOCK_SIZE
                              : (size_t)remaining_bytes;
            for (rec_off = 0; rec_off + 16u <= block_bytes; rec_off += 16u) {
                if (cb(block + rec_off,
                       (uint64_t)(lbn + b) * RSX11_BLOCK_SIZE + rec_off,
                       opaque) != 0) {
                    return -1;
                }
            }
            remaining_bytes -= block_bytes;
        }
    }

    return 0;
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_hiword32(const uint8_t *p)
{
    return ((uint32_t)get_le16(p) << 16) | (uint32_t)get_le16(p + 2u);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_hiword32(uint8_t *p, uint32_t v)
{
    put_le16(p, (uint16_t)(v >> 16));
    put_le16(p + 2u, (uint16_t)(v & 0xffffu));
}

static int seek_to(FILE *fp, uint64_t off)
{
    if (fseeko(fp, (off_t)off, SEEK_SET) != 0) {
        return -1;
    }
    return 0;
}

static int ensure_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (mkdir(path, 0777) != 0) {
        return -1;
    }
    return 0;
}

static int mkdir_parents(const char *path)
{
    char tmp[4096];
    size_t len;
    size_t i;

    len = strlen(path);
    if (len == 0u || len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, path, len + 1u);

    for (i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] != '\0' && ensure_dir(tmp) != 0) {
                return -1;
            }
            tmp[i] = '/';
        }
    }
    return ensure_dir(tmp);
}

static int read_at(FILE *fp, uint64_t off, void *buf, size_t len)
{
    if (seek_to(fp, off) != 0) {
        return -1;
    }
    if (fread(buf, 1, len, fp) != len) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int write_at(FILE *fp, uint64_t off, const void *buf, size_t len)
{
    if (seek_to(fp, off) != 0) {
        return -1;
    }
    if (fwrite(buf, 1, len, fp) != len) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static void copy_trim(char *dst, size_t dst_size,
                      const uint8_t *src, size_t src_len)
{
    size_t n = src_len;
    size_t i;

    while (n > 0 && (src[n - 1] == 0 || src[n - 1] == ' ')) {
        n--;
    }
    if (n >= dst_size) {
        n = dst_size - 1u;
    }
    for (i = 0; i < n; i++) {
        uint8_t c = src[i];
        dst[i] = (char)(isprint((int)c) ? c : ' ');
    }
    dst[n] = '\0';
}

static void trim_right(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') {
        s[--len] = '\0';
    }
}

static void copy_upper_zero_padded(uint8_t *dst, size_t dst_len,
                                   const char *src, size_t src_len)
{
    size_t i;

    memset(dst, 0, dst_len);
    for (i = 0; i < dst_len && i < src_len && src[i] != '\0'; i++) {
        dst[i] = (uint8_t)toupper((unsigned char)src[i]);
    }
}

static void copy_upper_space_padded(uint8_t *dst, size_t dst_len,
                                    const char *src, size_t src_len)
{
    size_t i;

    memset(dst, ' ', dst_len);
    for (i = 0; i < dst_len && i < src_len && src[i] != '\0'; i++) {
        dst[i] = (uint8_t)toupper((unsigned char)src[i]);
    }
}

static void format_volume_date(char out[15])
{
    static const char *months[12] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    time_t now;
    struct tm tm_now;

    now = time(NULL);
    if (localtime_r(&now, &tm_now) == NULL ||
        tm_now.tm_mon < 0 || tm_now.tm_mon >= 12) {
        snprintf(out, 15u, "01JAN70000000");
        return;
    }
    snprintf(out, 15u, "%02d%s%02d%02d%02d%02d",
             tm_now.tm_mday, months[tm_now.tm_mon],
             (tm_now.tm_year + 1900) % 100,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

static uint16_t header_checksum(const uint8_t *buf)
{
    uint16_t sum = 0;
    size_t i;

    for (i = 0; i < 255u; i++) {
        sum = (uint16_t)(sum + get_le16(buf + i * 2u));
    }
    return sum;
}

static uint16_t checksum_words(const uint8_t *buf, size_t word_count)
{
    uint16_t sum = 0;
    size_t i;

    for (i = 0; i < word_count; i++) {
        sum = (uint16_t)(sum + get_le16(buf + i * 2u));
    }
    return sum;
}

static void rad50_decode_word(uint16_t word, char out[4])
{
    static const char table[40] =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";
    unsigned int c0 = word / 1600u;
    unsigned int rem = word % 1600u;
    unsigned int c1 = rem / 40u;
    unsigned int c2 = rem % 40u;

    out[0] = table[c0 < 40u ? c0 : 0u];
    out[1] = table[c1 < 40u ? c1 : 0u];
    out[2] = table[c2 < 40u ? c2 : 0u];
    out[3] = '\0';
}

static void rad50_decode_name3(const uint8_t *p, char *out, size_t out_size)
{
    char part[4];
    size_t pos = 0;
    int i;

    if (out_size == 0) {
        return;
    }

    out[0] = '\0';
    for (i = 0; i < 3; i++) {
        rad50_decode_word(get_le16(p + (size_t)i * 2u), part);
        if (pos + 3u >= out_size) {
            break;
        }
        memcpy(out + pos, part, 3u);
        pos += 3u;
        out[pos] = '\0';
    }
    trim_right(out);
}

static int rad50_encode_char(int c)
{
    if (c == ' ' || c == '\0') {
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 1;
    }
    if (c == '$') {
        return 27;
    }
    if (c == '.') {
        return 28;
    }
    if (c == '%') {
        return 29;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 30;
    }
    return -1;
}

static int rad50_encode_word(const char *src, uint16_t *out)
{
    int v0;
    int v1;
    int v2;

    v0 = rad50_encode_char((unsigned char)src[0]);
    v1 = rad50_encode_char((unsigned char)src[1]);
    v2 = rad50_encode_char((unsigned char)src[2]);
    if (v0 < 0 || v1 < 0 || v2 < 0) {
        errno = EINVAL;
        return -1;
    }
    *out = (uint16_t)(v0 * 1600 + v1 * 40 + v2);
    return 0;
}

static int rad50_encode_name3(const char *src, size_t src_len, uint8_t *dst)
{
    char tmp[10];
    size_t i;

    if (src_len > 9u) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memset(tmp, ' ', sizeof(tmp));
    memcpy(tmp, src, src_len);
    for (i = 0; i < 3u; i++) {
        uint16_t word;

        if (rad50_encode_word(tmp + i * 3u, &word) != 0) {
            return -1;
        }
        put_le16(dst + i * 2u, word);
    }
    return 0;
}

static int same_fid(const rsx11_fid_t *a, const rsx11_fid_t *b)
{
    return a->num == b->num && a->seq == b->seq && a->rvn == b->rvn;
}

static int fid_is_null(const rsx11_fid_t *fid)
{
    return fid->num == 0u && fid->seq == 0u && fid->rvn == 0u;
}

static int header_contains_fid(const rsx11_file_header_t *hdr,
                               const rsx11_fid_t *fid)
{
    size_t i;

    for (i = 0; i < hdr->header_count; i++) {
        if (same_fid(&hdr->header_fids[i], fid)) {
            return 1;
        }
    }
    return 0;
}

static int append_header_segment(rsx11_file_header_t *dst,
                                 const rsx11_file_header_t *seg)
{
    if (dst->header_count >= RSX11_MAX_HEADER_SEGMENTS ||
        dst->extent_count + seg->extent_count > RSX11_MAX_EXTENTS) {
        errno = EFBIG;
        return -1;
    }
    if (header_contains_fid(dst, &seg->fid)) {
        errno = ELOOP;
        return -1;
    }

    if (seg->segment_number != 0u && dst->header_count > 0u &&
        seg->segment_number <= dst->segment_number) {
        errno = EINVAL;
        return -1;
    }

    memcpy(dst->extents + dst->extent_count,
           seg->extents, seg->extent_count * sizeof(seg->extents[0]));
    dst->extent_count += seg->extent_count;
    dst->segment_number = seg->segment_number;
    dst->next_fid = seg->next_fid;
    if (dst->size_bytes == 0u && seg->size_bytes != 0u) {
        dst->size_bytes = seg->size_bytes;
    }
    dst->header_fids[dst->header_count++] = seg->fid;
    return 0;
}

static int append_cached_header(rsx11_header_cache_t *cache,
                                const rsx11_file_header_t *hdr)
{
    rsx11_file_header_t *items;
    size_t new_cap;

    if (cache->count == cache->cap) {
        new_cap = cache->cap == 0u ? 128u : cache->cap * 2u;
        items = realloc(cache->items, new_cap * sizeof(*items));
        if (items == NULL) {
            return -1;
        }
        cache->items = items;
        cache->cap = new_cap;
    }

    cache->items[cache->count++] = *hdr;
    return 0;
}

static void free_header_cache(rsx11_header_cache_t *cache)
{
    free(cache->items);
    cache->items = NULL;
    cache->count = 0;
    cache->cap = 0;
    cache->have_home = 0;
    memset(&cache->home, 0, sizeof(cache->home));
}

static int parse_header_candidate(const uint8_t *buf, size_t avail,
                                  uint64_t raw_off,
                                  rsx11_file_header_t *out)
{
    uint8_t idof;
    uint8_t mpof;
    const uint8_t *ident;
    const uint8_t *map;
    size_t ptr_words;
    size_t ptr_count;
    size_t i;
    uint32_t fcs_hibk;
    uint32_t fcs_efbk;
    uint16_t fcs_ffby;
    uint64_t alloc_bytes = 0;

    if (avail < RSX11_HEADER_MIN) {
        return 0;
    }
    if (avail >= RSX11_BLOCK_SIZE &&
        header_checksum(buf) != get_le16(buf + 510u)) {
        return 0;
    }

    idof = buf[0];
    mpof = buf[1];
    if (idof < 10u || idof > 40u || mpof <= idof || mpof > 80u) {
        return 0;
    }
    if ((size_t)mpof * 2u + 10u > avail) {
        return 0;
    }
    if (get_le16(buf + 6u) != 0401u) {
        return 0;
    }
    if (get_le16(buf + 2u) == 0 || get_le16(buf + 4u) == 0) {
        return 0;
    }

    ident = buf + (size_t)idof * 2u;
    map = buf + (size_t)mpof * 2u;
    if (ident + 10u > map) {
        return 0;
    }

    if (map[6] != 1u || map[7] != 3u) {
        return 0;
    }
    ptr_words = map[8];
    if ((ptr_words & 1u) != 0u || ptr_words > map[9]) {
        return 0;
    }

    ptr_count = ptr_words / 2u;
    if (ptr_count > RSX11_MAX_EXTENTS) {
        return 0;
    }
    if ((size_t)mpof * 2u + 10u + ptr_count * 4u > avail) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->raw_offset = raw_off;
    out->fid.num = get_le16(buf + 2u);
    out->fid.seq = get_le16(buf + 4u);
    out->fid.rvn = 0;
    out->segment_number = map[0];
    out->next_fid.rvn = map[1];
    out->next_fid.num = get_le16(map + 2u);
    out->next_fid.seq = get_le16(map + 4u);
    out->header_count = 1u;
    out->header_fids[0] = out->fid;
    rad50_decode_name3(ident, out->name, sizeof(out->name));
    rad50_decode_name3(ident + 6u, out->ext, sizeof(out->ext));
    out->ext[3] = '\0';
    trim_right(out->ext);
    out->version = get_le16(ident + 8u);
    out->extent_count = ptr_count;
    out->size_bytes = 0;

    if (out->name[0] == '\0' || out->version == 0u) {
        return 0;
    }

    for (i = 0; i < ptr_count; i++) {
        const uint8_t *ptr = map + 10u + i * 4u;
        out->extents[i].lbn =
            ((uint32_t)ptr[0] << 16) | (uint32_t)get_le16(ptr + 2u);
        out->extents[i].blocks = (uint16_t)ptr[1] + 1u;
        alloc_bytes += (uint64_t)out->extents[i].blocks * RSX11_BLOCK_SIZE;
    }

    if (RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_FFBY + 2u <= avail) {
        const uint8_t *fcs = buf + RSX11_HDR_OFF_UFAT;

        fcs_hibk = get_hiword32(fcs + RSX11_FCS_OFF_HIBK);
        fcs_efbk = get_hiword32(fcs + RSX11_FCS_OFF_EFBK);
        fcs_ffby = get_le16(fcs + RSX11_FCS_OFF_FFBY);

        if (fcs_hibk > 0u && fcs_efbk > 0u && fcs_ffby <= RSX11_BLOCK_SIZE) {
            uint64_t size_bytes =
                (uint64_t)(fcs_efbk - 1u) * RSX11_BLOCK_SIZE + fcs_ffby;

            if (size_bytes <= alloc_bytes ||
                out->next_fid.num != 0u || out->next_fid.seq != 0u ||
                out->next_fid.rvn != 0u) {
                out->size_bytes = size_bytes;
            }
        }
        if (out->size_bytes == 0 && alloc_bytes > 0 &&
            fcs_ffby == 0u && fcs_efbk > 0u) {
            uint64_t size_bytes =
                (uint64_t)(fcs_efbk - 1u) * RSX11_BLOCK_SIZE;

            if (size_bytes <= alloc_bytes ||
                out->next_fid.num != 0u || out->next_fid.seq != 0u ||
                out->next_fid.rvn != 0u) {
                out->size_bytes = size_bytes;
            }
        }
    }

    return 1;
}

static const rsx11_file_header_t *find_cached_header(
    const rsx11_header_cache_t *cache, const rsx11_fid_t *fid)
{
    size_t i;

    for (i = 0; i < cache->count; i++) {
        if (same_fid(&cache->items[i].fid, fid)) {
            return &cache->items[i];
        }
    }

    return NULL;
}

static int load_home_cached(rsx11_image_t *img, rsx11_header_cache_t *cache)
{
    if (!cache->have_home) {
        if (rsx11_read_home(img, &cache->home) != 0) {
            return -1;
        }
        cache->have_home = 1;
    }
    return 0;
}

static int load_index_bitmap(rsx11_image_t *img, const rsx11_home_t *home,
                             uint8_t **bitmap_out, size_t *size_out)
{
    uint8_t *bitmap;
    size_t size;

    size = (size_t)home->ibsz * RSX11_BLOCK_SIZE;
    if (size == 0u) {
        errno = EINVAL;
        return -1;
    }
    bitmap = malloc(size);
    if (bitmap == NULL) {
        return -1;
    }
    if (read_at(img->fp, (uint64_t)home->iblb * RSX11_BLOCK_SIZE,
                bitmap, size) != 0) {
        free(bitmap);
        return -1;
    }
    *bitmap_out = bitmap;
    if (size_out != NULL) {
        *size_out = size;
    }
    return 0;
}

static int index_bitmap_test(const uint8_t *bitmap, size_t size, uint16_t fnum)
{
    size_t bit;

    if (fnum == 0u) {
        return 0;
    }
    bit = (size_t)fnum - 1u;
    if (bit / 8u >= size) {
        return 0;
    }
    return (bitmap[bit / 8u] & (uint8_t)(1u << (bit & 7u))) != 0;
}

static void index_bitmap_set(uint8_t *bitmap, size_t size,
                             uint16_t fnum, int in_use)
{
    size_t bit;
    uint8_t mask;

    if (fnum == 0u) {
        return;
    }
    bit = (size_t)fnum - 1u;
    if (bit / 8u >= size) {
        return;
    }
    mask = (uint8_t)(1u << (bit & 7u));
    if (in_use) {
        bitmap[bit / 8u] |= mask;
    } else {
        bitmap[bit / 8u] &= (uint8_t)~mask;
    }
}

static int lookup_header(rsx11_image_t *img, rsx11_header_cache_t *cache,
                         const rsx11_fid_t *fid, rsx11_file_header_t *out)
{
    const rsx11_file_header_t *cached;
    rsx11_file_header_t hdr;
    rsx11_file_header_t seg;
    rsx11_fid_t next_fid;
    size_t guard;

    cached = find_cached_header(cache, fid);
    if (cached != NULL) {
        *out = *cached;
        return 0;
    }

    if (load_home_cached(img, cache) != 0) {
        return -1;
    }
    if (read_header_slot(img, &cache->home, fid->num, &hdr) != 0) {
        return -1;
    }
    if (!same_fid(&hdr.fid, fid)) {
        errno = ENOENT;
        return -1;
    }
    next_fid = hdr.next_fid;
    for (guard = 0u; !fid_is_null(&next_fid); guard++) {
        if (guard >= RSX11_MAX_HEADER_SEGMENTS - 1u) {
            errno = ELOOP;
            return -1;
        }
        if (read_header_slot(img, &cache->home, next_fid.num, &seg) != 0) {
            return -1;
        }
        if (!same_fid(&seg.fid, &next_fid)) {
            errno = ENOENT;
            return -1;
        }
        if (append_header_segment(&hdr, &seg) != 0) {
            return -1;
        }
        next_fid = hdr.next_fid;
    }
    memset(&hdr.next_fid, 0, sizeof(hdr.next_fid));
    if (append_cached_header(cache, &hdr) != 0) {
        return -1;
    }

    *out = hdr;
    return 0;
}

static int map_vbn_to_lbn(const rsx11_file_header_t *hdr,
                          uint32_t vbn, uint32_t *lbn_out)
{
    uint32_t cur_vbn = 1u;
    size_t i;

    if (vbn == 0u) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < hdr->extent_count; i++) {
        uint32_t blocks = hdr->extents[i].blocks;

        if (blocks == 0u) {
            continue;
        }
        if (vbn >= cur_vbn && vbn < cur_vbn + blocks) {
            *lbn_out = hdr->extents[i].lbn + (vbn - cur_vbn);
            return 0;
        }
        cur_vbn += blocks;
    }

    errno = ENOENT;
    return -1;
}

static int read_header_slot_direct(rsx11_image_t *img, const rsx11_home_t *home,
                                   uint16_t fnum, uint8_t block[RSX11_BLOCK_SIZE])
{
    uint32_t lbn;

    if (fnum == 0u || fnum > 16u) {
        errno = ENOENT;
        return -1;
    }
    lbn = home->iblb + (uint32_t)home->ibsz + (uint32_t)fnum - 1u;
    if ((uint64_t)(lbn + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    return read_at(img->fp, (uint64_t)lbn * RSX11_BLOCK_SIZE,
                   block, RSX11_BLOCK_SIZE);
}

static int read_header_at_lbn(rsx11_image_t *img, uint32_t lbn,
                              uint16_t fnum, rsx11_file_header_t *out)
{
    uint8_t block[RSX11_BLOCK_SIZE];

    if ((uint64_t)(lbn + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    if (read_at(img->fp, (uint64_t)lbn * RSX11_BLOCK_SIZE,
                block, sizeof(block)) != 0) {
        return -1;
    }
    if (!parse_header_candidate(block, sizeof(block),
                                (uint64_t)lbn * RSX11_BLOCK_SIZE, out)) {
        errno = ENOENT;
        return -1;
    }
    if (out->fid.num != fnum) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int header_slot_lbn(rsx11_image_t *img, const rsx11_home_t *home,
                           uint16_t fnum, uint32_t *lbn_out)
{
    uint8_t block[RSX11_BLOCK_SIZE];
    rsx11_file_header_t index_hdr;
    uint32_t header_vbn;
    rsx11_fid_t next_fid;
    rsx11_file_header_t seg;
    size_t guard;

    if (fnum == 0u) {
        errno = ENOENT;
        return -1;
    }
    if (fnum <= 16u) {
        *lbn_out = home->iblb + (uint32_t)home->ibsz + (uint32_t)fnum - 1u;
        return 0;
    }

    if (read_header_slot_direct(img, home, 1u, block) != 0) {
        return -1;
    }
    if (!parse_header_candidate(block, sizeof(block),
                                (uint64_t)(home->iblb + (uint32_t)home->ibsz) *
                                    RSX11_BLOCK_SIZE,
                                &index_hdr) ||
        index_hdr.fid.num != 1u || index_hdr.fid.seq != 1u) {
        errno = EINVAL;
        return -1;
    }
    next_fid = index_hdr.next_fid;
    for (guard = 0u; !fid_is_null(&next_fid); guard++) {
        uint32_t next_lbn;
        uint32_t next_vbn;

        if (guard >= RSX11_MAX_HEADER_SEGMENTS - 1u) {
            errno = ELOOP;
            return -1;
        }
        next_vbn = 2u + (uint32_t)home->ibsz + (uint32_t)next_fid.num;
        if (map_vbn_to_lbn(&index_hdr, next_vbn, &next_lbn) != 0) {
            return -1;
        }
        if (read_header_at_lbn(img, next_lbn, next_fid.num, &seg) != 0) {
            return -1;
        }
        if (!same_fid(&seg.fid, &next_fid)) {
            errno = ENOENT;
            return -1;
        }
        if (append_header_segment(&index_hdr, &seg) != 0) {
            return -1;
        }
        next_fid = index_hdr.next_fid;
    }

    header_vbn = 2u + (uint32_t)home->ibsz + (uint32_t)fnum;
    return map_vbn_to_lbn(&index_hdr, header_vbn, lbn_out);
}

static int read_header_slot_block(rsx11_image_t *img, const rsx11_home_t *home,
                                  uint16_t fnum, uint8_t block[RSX11_BLOCK_SIZE])
{
    uint32_t lbn;

    if (header_slot_lbn(img, home, fnum, &lbn) != 0) {
        return -1;
    }
    if ((uint64_t)(lbn + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    return read_at(img->fp, (uint64_t)lbn * RSX11_BLOCK_SIZE,
                   block, RSX11_BLOCK_SIZE);
}

static int read_header_slot(rsx11_image_t *img, const rsx11_home_t *home,
                            uint16_t fnum, rsx11_file_header_t *out)
{
    uint32_t lbn;

    if (header_slot_lbn(img, home, fnum, &lbn) != 0) {
        return -1;
    }
    return read_header_at_lbn(img, lbn, fnum, out);
}

static int decode_dir_record(const uint8_t *rec, rsx11_dir_record_t *out)
{
    if (dir_record_is_empty(rec)) {
        memset(out, 0, sizeof(*out));
        return 0;
    }

    decode_dir_record_loose(rec, out);
    if (out->fid.num == 0u || out->name[0] == '\0' || out->version == 0u) {
        return 0;
    }
    return 1;
}

static int append_dirent(rsx11_dirlist_t *list, const rsx11_dirent_t *ent)
{
    rsx11_dirent_t *items;
    size_t new_cap;

    if (list->count == 0u) {
        list->entries = NULL;
    }
    if ((list->count & (list->count - 1u)) == 0u) {
        new_cap = list->count == 0u ? 128u : list->count * 2u;
        items = realloc(list->entries, new_cap * sizeof(*items));
        if (items == NULL) {
            return -1;
        }
        list->entries = items;
    }
    list->entries[list->count++] = *ent;
    return 0;
}

static uint32_t header_total_blocks(const rsx11_file_header_t *hdr)
{
    uint32_t total = 0;
    size_t i;

    for (i = 0; i < hdr->extent_count; i++) {
        total += hdr->extents[i].blocks;
    }
    return total;
}

typedef struct {
    const char *dir_name;
    rsx11_dirlist_t *out;
} rsx11_dir_read_ctx_t;

typedef struct {
    rsx11_image_t *img;
    const char *dir_name;
    int repair;
    FILE *out;
    rsx11_fsck_report_t *report;
    int *dir_dirty;
    rsx11_dirlist_t *entries;
} rsx11_fsck_dir_ctx_t;

static int collect_dir_record_cb(const uint8_t *rec, uint64_t raw_offset,
                                 void *opaque)
{
    rsx11_dir_read_ctx_t *ctx = opaque;
    rsx11_dir_record_t dir_rec;
    rsx11_dirent_t ent;

    if (!decode_dir_record(rec, &dir_rec)) {
        return 0;
    }

    memset(&ent, 0, sizeof(ent));
    strncpy(ent.dir, ctx->dir_name, sizeof(ent.dir) - 1u);
    strncpy(ent.name, dir_rec.name, sizeof(ent.name) - 1u);
    strncpy(ent.ext, dir_rec.ext, sizeof(ent.ext) - 1u);
    ent.version = dir_rec.version;
    ent.fid = dir_rec.fid;
    ent.raw_offset = raw_offset;

    if (append_dirent(ctx->out, &ent) != 0) {
        return -1;
    }
    return 0;
}

static int fsck_dir_record_cb(const uint8_t *rec, uint64_t raw_offset,
                              void *opaque)
{
    rsx11_fsck_dir_ctx_t *ctx = opaque;
    rsx11_dir_record_t dir_rec;
    rsx11_dirent_t ent;

    if (dir_record_is_empty(rec)) {
        return 0;
    }

    memset(&ent, 0, sizeof(ent));
    strncpy(ent.dir, ctx->dir_name, sizeof(ent.dir) - 1u);
    ent.raw_offset = raw_offset;

    decode_dir_record_loose(rec, &dir_rec);
    strncpy(ent.name, dir_rec.name, sizeof(ent.name) - 1u);
    strncpy(ent.ext, dir_rec.ext, sizeof(ent.ext) - 1u);
    ent.version = dir_rec.version;
    ent.fid = dir_rec.fid;

    if (dir_rec.fid.num == 0u || dir_rec.name[0] == '\0' ||
        dir_rec.version == 0u) {
        ctx->report->checked++;
        fsck_issue(ctx->out, ctx->report, &ent,
                   dir_rec.fid.num == 0u
                       ? "directory record references invalid FID"
                       : "directory record is malformed");
        if (ctx->repair) {
            if (fsck_clear_dir_record(ctx->img, &ent) != 0) {
                fsck_fatal(ctx->out, ctx->report, &ent,
                           "cannot remove malformed directory record");
            } else {
                *ctx->dir_dirty = 1;
                ctx->report->repaired++;
            }
        } else {
            ctx->report->fatal++;
        }
        return 0;
    }

    if (append_dirent(ctx->entries, &ent) != 0) {
        return -1;
    }
    return 0;
}

static int format_uic(const char *name, char *out, size_t out_size)
{
    char group[4];
    char user[4];
    size_t i;

    if (strlen(name) != 6u) {
        return 0;
    }
    for (i = 0; i < 6u; i++) {
        if (name[i] < '0' || name[i] > '7') {
            return 0;
        }
    }
    memcpy(group, name, 3u);
    memcpy(user, name + 3u, 3u);
    group[3] = '\0';
    user[3] = '\0';
    snprintf(out, out_size, "[%s,%s]", group, user);
    return 1;
}

static void host_dir_name(const char *dir, char *out, size_t out_size)
{
    size_t i;
    size_t pos = 0;

    if (strcmp(dir, "MFD") == 0) {
        snprintf(out, out_size, "MFD");
        return;
    }

    for (i = 0; dir[i] != '\0' && pos + 1u < out_size; i++) {
        char c = dir[i];

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z')) {
            out[pos++] = c;
        } else if (c == ',') {
            out[pos++] = '_';
        }
    }
    out[pos] = '\0';
}

static void format_host_file_name(const rsx11_dirent_t *ent,
                                  char *out, size_t out_size)
{
    if (ent->ext[0] != '\0') {
        snprintf(out, out_size, "%s.%s;%u",
                 ent->name, ent->ext, (unsigned)ent->version);
    } else {
        snprintf(out, out_size, "%s;%u",
                 ent->name, (unsigned)ent->version);
    }
}

static void format_entry_selector(const rsx11_dirent_t *ent,
                                  char *out, size_t out_size)
{
    if (strcmp(ent->dir, "MFD") == 0) {
        format_host_file_name(ent, out, out_size);
    } else {
        char spec[32];

        format_host_file_name(ent, spec, sizeof(spec));
        snprintf(out, out_size, "%s%s", ent->dir, spec);
    }
}

static void format_header_selector(const rsx11_file_header_t *hdr,
                                   char *out, size_t out_size)
{
    if (hdr->ext[0] != '\0') {
        snprintf(out, out_size, "%s.%s;%u (%06o,%06o,%06o)",
                 hdr->name, hdr->ext, (unsigned)hdr->version,
                 hdr->fid.num, hdr->fid.seq, hdr->fid.rvn);
    } else {
        snprintf(out, out_size, "%s;%u (%06o,%06o,%06o)",
                 hdr->name, (unsigned)hdr->version,
                 hdr->fid.num, hdr->fid.seq, hdr->fid.rvn);
    }
}

static int read_dir_records_raw(rsx11_image_t *img, const rsx11_file_header_t *hdr,
                                const char *dir_name, rsx11_dirlist_t *out)
{
    rsx11_dir_read_ctx_t ctx;

    ctx.dir_name = dir_name;
    ctx.out = out;
    return walk_dir_records(img, hdr, collect_dir_record_cb, &ctx);
}

static int read_dir_file(rsx11_image_t *img, const rsx11_file_header_t *hdr,
                         rsx11_header_cache_t *cache, const char *dir_name,
                         rsx11_dirlist_t *out)
{
    rsx11_dirlist_t raw;
    size_t i;

    memset(&raw, 0, sizeof(raw));
    if (read_dir_records_raw(img, hdr, dir_name, &raw) != 0) {
        return -1;
    }

    for (i = 0; i < raw.count; i++) {
        rsx11_file_header_t file_hdr;
        rsx11_dirent_t ent;

        if (lookup_header(img, cache, &raw.entries[i].fid, &file_hdr) != 0) {
            continue;
        }
        ent = raw.entries[i];
        ent.blocks = header_total_blocks(&file_hdr);
        if (append_dirent(out, &ent) != 0) {
            rsx11_free_dirlist(&raw);
            return -1;
        }
    }

    rsx11_free_dirlist(&raw);
    return 0;
}

static int extract_header_to_path(rsx11_image_t *img,
                                  const rsx11_file_header_t *hdr,
                                  const char *path)
{
    uint8_t block[RSX11_BLOCK_SIZE];
    FILE *out;
    size_t i;
    uint32_t b;
    uint64_t remaining_bytes;

    out = fopen(path, "wb");
    if (out == NULL) {
        return -1;
    }

    remaining_bytes = hdr->size_bytes;
    if (remaining_bytes == 0u) {
        for (i = 0; i < hdr->extent_count; i++) {
            remaining_bytes +=
                (uint64_t)hdr->extents[i].blocks * RSX11_BLOCK_SIZE;
        }
    }

    for (i = 0; i < hdr->extent_count; i++) {
        uint32_t lbn = hdr->extents[i].lbn;
        uint32_t count = hdr->extents[i].blocks;

        for (b = 0; b < count; b++) {
            size_t to_write;

            if (remaining_bytes == 0u) {
                break;
            }
            if ((uint64_t)(lbn + b + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
                fclose(out);
                errno = EIO;
                return -1;
            }
            if (read_at(img->fp, (uint64_t)(lbn + b) * RSX11_BLOCK_SIZE,
                        block, sizeof(block)) != 0) {
                fclose(out);
                return -1;
            }
            to_write = (remaining_bytes >= RSX11_BLOCK_SIZE)
                           ? RSX11_BLOCK_SIZE
                           : (size_t)remaining_bytes;
            if (fwrite(block, 1, to_write, out) != to_write) {
                fclose(out);
                errno = EIO;
                return -1;
            }
            remaining_bytes -= to_write;
        }
    }

    if (fclose(out) != 0) {
        return -1;
    }
    return 0;
}

static int read_header_data(rsx11_image_t *img, const rsx11_file_header_t *hdr,
                            uint8_t *buf, size_t len)
{
    size_t pos = 0;
    size_t i;
    uint32_t b;

    for (i = 0; i < hdr->extent_count && pos < len; i++) {
        uint32_t lbn = hdr->extents[i].lbn;
        uint32_t count = hdr->extents[i].blocks;

        for (b = 0; b < count && pos < len; b++) {
            size_t chunk = len - pos;

            if (chunk > RSX11_BLOCK_SIZE) {
                chunk = RSX11_BLOCK_SIZE;
            }
            if (read_at(img->fp, (uint64_t)(lbn + b) * RSX11_BLOCK_SIZE,
                        buf + pos, chunk) != 0) {
                return -1;
            }
            pos += chunk;
        }
    }

    if (pos != len) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int write_header_data(rsx11_image_t *img, const rsx11_file_header_t *hdr,
                             const uint8_t *buf, size_t len)
{
    size_t pos = 0;
    size_t i;
    uint32_t b;

    for (i = 0; i < hdr->extent_count && pos < len; i++) {
        uint32_t lbn = hdr->extents[i].lbn;
        uint32_t count = hdr->extents[i].blocks;

        for (b = 0; b < count && pos < len; b++) {
            size_t chunk = len - pos;

            if (chunk > RSX11_BLOCK_SIZE) {
                chunk = RSX11_BLOCK_SIZE;
            }
            if (write_at(img->fp, (uint64_t)(lbn + b) * RSX11_BLOCK_SIZE,
                         buf + pos, chunk) != 0) {
                return -1;
            }
            pos += chunk;
        }
    }

    if (pos != len) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int write_header_slot(rsx11_image_t *img, const rsx11_home_t *home,
                             uint16_t fnum, const uint8_t block[RSX11_BLOCK_SIZE])
{
    uint32_t lbn;

    if (fnum == 0u) {
        errno = EINVAL;
        return -1;
    }
    if (header_slot_lbn(img, home, fnum, &lbn) != 0) {
        return -1;
    }
    return write_at(img->fp, (uint64_t)lbn * RSX11_BLOCK_SIZE,
                    block, RSX11_BLOCK_SIZE);
}

static int parse_uic_word(const char *dir, uint16_t *uic_out)
{
    unsigned int group;
    unsigned int user;

    if (strcmp(dir, "MFD") == 0) {
        *uic_out = 0;
        return 0;
    }
    if (sscanf(dir, "[%o,%o]", &group, &user) != 2 ||
        group > 0377u || user > 0377u) {
        errno = EINVAL;
        return -1;
    }
    *uic_out = (uint16_t)((group << 8) | user);
    return 0;
}

static int load_storage_bitmap(rsx11_image_t *img, rsx11_header_cache_t *cache,
                               uint8_t **data_out, size_t *data_size_out,
                               rsx11_file_header_t *hdr_out,
                               uint32_t *unit_blocks_out)
{
    rsx11_fid_t fid;
    rsx11_file_header_t hdr;
    uint8_t *data;
    size_t size;
    uint8_t bitmap_blocks;
    size_t unit_off;

    memset(&fid, 0, sizeof(fid));
    fid.num = 2u;
    fid.seq = 2u;
    if (lookup_header(img, cache, &fid, &hdr) != 0) {
        return -1;
    }

    size = (size_t)header_total_blocks(&hdr) * RSX11_BLOCK_SIZE;
    data = malloc(size);
    if (data == NULL) {
        return -1;
    }
    if (read_header_data(img, &hdr, data, size) != 0) {
        free(data);
        return -1;
    }

    bitmap_blocks = data[3];
    unit_off = 4u + (size_t)bitmap_blocks * 4u;
    if (size < RSX11_BLOCK_SIZE * 2u || unit_off + 4u > RSX11_BLOCK_SIZE) {
        free(data);
        errno = EINVAL;
        return -1;
    }

    *data_out = data;
    *data_size_out = size;
    if (hdr_out != NULL) {
        *hdr_out = hdr;
    }
    if (unit_blocks_out != NULL) {
        *unit_blocks_out = get_hiword32(data + unit_off);
    }
    return 0;
}

static int storage_bitmap_is_free(const uint8_t *data, size_t data_size,
                                  uint32_t blockno)
{
    size_t bit = (size_t)blockno;
    size_t byte_off = RSX11_BLOCK_SIZE + bit / 8u;

    if (byte_off >= data_size) {
        return 0;
    }
    return (data[byte_off] & (uint8_t)(1u << (bit & 7u))) != 0;
}

static void storage_bitmap_set_free(uint8_t *data, size_t data_size,
                                    uint32_t blockno, int is_free)
{
    size_t bit = (size_t)blockno;
    size_t byte_off = RSX11_BLOCK_SIZE + bit / 8u;
    uint8_t mask;

    if (byte_off >= data_size) {
        return;
    }
    mask = (uint8_t)(1u << (bit & 7u));
    if (is_free) {
        data[byte_off] |= mask;
    } else {
        data[byte_off] &= (uint8_t)~mask;
    }
}

static int release_header_chain_allocation(const rsx11_file_header_t *hdr,
                                           uint8_t *index_bitmap,
                                           size_t index_size,
                                           uint8_t *storage_bitmap,
                                           size_t storage_size,
                                           uint32_t unit_blocks)
{
    size_t i;

    if (hdr == NULL || index_bitmap == NULL || storage_bitmap == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < hdr->extent_count; i++) {
        uint32_t lbn = hdr->extents[i].lbn;
        uint32_t count = hdr->extents[i].blocks;
        uint32_t b;

        if (count == 0u) {
            continue;
        }
        if (lbn + count > unit_blocks) {
            errno = EIO;
            return -1;
        }
        for (b = 0; b < count; b++) {
            storage_bitmap_set_free(storage_bitmap, storage_size,
                                    lbn + b, 1);
        }
    }

    for (i = 0; i < hdr->header_count; i++) {
        index_bitmap_set(index_bitmap, index_size,
                         hdr->header_fids[i].num, 0);
    }
    return 0;
}

static int find_free_blocks(uint8_t *data, size_t data_size,
                            uint32_t unit_blocks, uint32_t needed,
                            uint32_t *start_out)
{
    uint32_t start = 0;
    uint32_t run = 0;
    uint32_t b;

    for (b = 0; b < unit_blocks; b++) {
        if (storage_bitmap_is_free(data, data_size, b)) {
            if (run == 0u) {
                start = b;
            }
            run++;
            if (run >= needed) {
                *start_out = start;
                return 0;
            }
        } else {
            run = 0;
        }
    }

    errno = ENOSPC;
    return -1;
}

static int allocate_file_extents(const uint8_t *data, size_t data_size,
                                 uint32_t unit_blocks, uint32_t needed,
                                 rsx11_extent_t *extents,
                                 size_t *extent_count_out)
{
    size_t extent_count = 0;
    uint32_t run_start = 0;
    uint32_t run_blocks = 0;
    uint32_t b;

    if (extents == NULL || extent_count_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (needed == 0u) {
        *extent_count_out = 0u;
        return 0;
    }

    for (b = 0; b <= unit_blocks; b++) {
        int is_free = (b < unit_blocks) &&
                      storage_bitmap_is_free(data, data_size, b);

        if (is_free) {
            if (run_blocks == 0u) {
                run_start = b;
            }
            run_blocks++;
            continue;
        }
        if (run_blocks != 0u) {
            uint32_t take = run_blocks > needed ? needed : run_blocks;
            uint32_t pos = run_start;

            while (take != 0u) {
                uint32_t chunk = take > 256u ? 256u : take;

                if (extent_count >= RSX11_MAX_EXTENTS) {
                    errno = EFBIG;
                    return -1;
                }
                extents[extent_count].lbn = pos;
                extents[extent_count].blocks = (uint16_t)chunk;
                extent_count++;
                pos += chunk;
                take -= chunk;
                needed -= chunk;
            }
            if (needed == 0u) {
                *extent_count_out = extent_count;
                return 0;
            }
            run_blocks = 0u;
        }
    }

    errno = ENOSPC;
    return -1;
}

static int read_slot_sequence(rsx11_image_t *img, const rsx11_home_t *home,
                              uint16_t fnum, uint16_t *seq_out)
{
    uint8_t block[RSX11_BLOCK_SIZE];

    if (read_header_slot_block(img, home, fnum, block) != 0) {
        return -1;
    }
    *seq_out = get_le16(block + 4u);
    return 0;
}

static uint32_t header_map_pointer_count(uint32_t blocks)
{
    return (blocks + 255u) / 256u;
}

static int header_segment_count_for_extents(const rsx11_extent_t *extents,
                                            size_t extent_count,
                                            size_t *segment_count_out)
{
    uint32_t ptr_count = 0;
    size_t i;

    if (segment_count_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < extent_count; i++) {
        ptr_count += header_map_pointer_count(extents[i].blocks);
    }
    if (ptr_count == 0u) {
        *segment_count_out = 1u;
        return 0;
    }

    *segment_count_out =
        (size_t)((ptr_count + (RSX11_MAP_MAX_WORDS / 2u) - 1u) /
                 (RSX11_MAP_MAX_WORDS / 2u));
    if (*segment_count_out > RSX11_MAX_HEADER_SEGMENTS) {
        errno = EFBIG;
        return -1;
    }
    return 0;
}

static int build_file_header_extents(uint8_t block[RSX11_BLOCK_SIZE],
                                     uint16_t fnum, uint16_t seq,
                                     uint16_t owner_uic, uint16_t prot,
                                     const char *name, const char *ext,
                                     uint16_t version,
                                     const rsx11_extent_t *extents,
                                     size_t extent_count,
                                     uint64_t file_bytes)
{
    static const size_t idof = 23u;
    static const size_t mpof = 46u;
    uint8_t *ident;
    uint8_t *map;
    uint32_t ptr_count = 0;
    uint32_t total_blocks = 0;
    uint32_t efbk;
    uint16_t ffby;
    size_t ptr_idx;
    size_t i;

    memset(block, 0, RSX11_BLOCK_SIZE);
    block[0] = (uint8_t)idof;
    block[1] = (uint8_t)mpof;
    put_le16(block + 2u, fnum);
    put_le16(block + 4u, seq);
    put_le16(block + 6u, 0401u);
    put_le16(block + 8u, owner_uic);
    put_le16(block + 10u, prot);
    put_le16(block + 12u, 0u);

    ident = block + idof * 2u;
    if (rad50_encode_name3(name, strlen(name), ident) != 0) {
        return -1;
    }
    if (rad50_encode_name3(ext, strlen(ext), ident + 6u) != 0) {
        return -1;
    }
    put_le16(ident + 8u, version);

    for (i = 0; i < extent_count; i++) {
        if (extents[i].blocks == 0u) {
            errno = EINVAL;
            return -1;
        }
        total_blocks += extents[i].blocks;
        ptr_count += header_map_pointer_count(extents[i].blocks);
    }
    if (ptr_count == 0u && (extent_count != 0u || file_bytes != 0u)) {
        errno = EINVAL;
        return -1;
    }
    if (ptr_count * 2u > RSX11_MAP_MAX_WORDS) {
        errno = EFBIG;
        return -1;
    }

    ffby = (uint16_t)(file_bytes % RSX11_BLOCK_SIZE);
    efbk = total_blocks == 0u ? 0u : (uint32_t)((file_bytes + RSX11_BLOCK_SIZE - 1u) /
                                                 RSX11_BLOCK_SIZE);
    if (total_blocks != 0u && ffby == 0u) {
        ffby = RSX11_BLOCK_SIZE;
    }
    block[RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_RTYP] = 1u;
    put_le16(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_RSIZ,
             RSX11_BLOCK_SIZE);
    put_hiword32(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_HIBK, total_blocks);
    put_hiword32(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_EFBK, efbk);
    put_le16(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_FFBY, ffby);

    map = block + mpof * 2u;
    map[6] = 1u;
    map[7] = 3u;
    map[8] = (uint8_t)(ptr_count * 2u);
    map[9] = RSX11_MAP_MAX_WORDS;

    ptr_idx = 0u;
    for (i = 0; i < extent_count; i++) {
        uint32_t rem_blocks = extents[i].blocks;
        uint32_t lbn = extents[i].lbn;

        while (rem_blocks != 0u) {
            uint32_t seg = rem_blocks > 256u ? 256u : rem_blocks;
            uint8_t *ptr = map + 10u + ptr_idx * 4u;

            ptr[0] = (uint8_t)(lbn >> 16);
            ptr[1] = (uint8_t)(seg - 1u);
            put_le16(ptr + 2u, (uint16_t)(lbn & 0xffffu));
            lbn += seg;
            rem_blocks -= seg;
            ptr_idx++;
        }
    }

    put_le16(block + 510u, header_checksum(block));
    return 0;
}

static int build_file_header_chain_segment(uint8_t block[RSX11_BLOCK_SIZE],
                                           uint16_t fnum, uint16_t seq,
                                           uint16_t owner_uic,
                                           uint16_t prot,
                                           const char *name,
                                           const char *ext,
                                           uint16_t version,
                                           const rsx11_extent_t *extents,
                                           size_t extent_count,
                                           uint64_t file_bytes,
                                           uint8_t segment_number,
                                           const rsx11_fid_t *next_fid)
{
    uint8_t *map;

    if (build_file_header_extents(block, fnum, seq, owner_uic, prot,
                                  name, ext, version,
                                  extents, extent_count, file_bytes) != 0) {
        return -1;
    }
    map = block + (size_t)block[1] * 2u;
    map[0] = segment_number;
    if (next_fid != NULL) {
        map[1] = (uint8_t)next_fid->rvn;
        put_le16(map + 2u, next_fid->num);
        put_le16(map + 4u, next_fid->seq);
    }
    put_le16(block + 510u, header_checksum(block));
    return 0;
}

static int build_file_header(uint8_t block[RSX11_BLOCK_SIZE],
                             uint16_t fnum, uint16_t seq, uint16_t owner_uic,
                             uint16_t prot, const char *name, const char *ext,
                             uint16_t version, uint32_t start_lbn,
                             uint32_t blocks, uint64_t file_bytes)
{
    rsx11_extent_t extent;

    if (blocks == 0u && file_bytes == 0u) {
        return build_file_header_extents(block, fnum, seq, owner_uic, prot,
                                         name, ext, version,
                                         NULL, 0u, 0u);
    }

    extent.lbn = start_lbn;
    extent.blocks = (uint16_t)blocks;
    return build_file_header_extents(block, fnum, seq, owner_uic, prot,
                                     name, ext, version,
                                     &extent, 1u, file_bytes);
}

static void build_home_block(uint8_t block[RSX11_BLOCK_SIZE],
                             uint16_t ibsz, uint32_t iblb,
                             uint16_t fmax, const char *label)
{
    char created[15];

    memset(block, 0, RSX11_BLOCK_SIZE);
    put_le16(block + RSX11_HOME_OFF_IBSZ, ibsz);
    put_hiword32(block + RSX11_HOME_OFF_IBLB, iblb);
    put_le16(block + RSX11_HOME_OFF_FMAX, fmax);
    put_le16(block + RSX11_HOME_OFF_SBCL, 1u);
    put_le16(block + RSX11_HOME_OFF_DVTY, 0u);
    put_le16(block + RSX11_HOME_OFF_VLEV, 0401u);
    copy_upper_zero_padded(block + RSX11_HOME_OFF_VNAM, 12u,
                           label, strlen(label));
    put_le16(block + RSX11_HOME_OFF_VOWN, 0401u);
    put_le16(block + RSX11_HOME_OFF_VPRO, 0u);
    put_le16(block + RSX11_HOME_OFF_VCHA, 000030u);
    put_le16(block + RSX11_HOME_OFF_DFPR, 0160000u);
    block[RSX11_HOME_OFF_WISZ] = 7u;
    block[RSX11_HOME_OFF_FIEX] = 5u;
    block[RSX11_HOME_OFF_LRUC] = 3u;
    format_volume_date(created);
    memcpy(block + RSX11_HOME_OFF_VDAT, created, 14u);
    copy_upper_space_padded(block + RSX11_HOME_OFF_INDN, 12u,
                            label, strlen(label));
    memcpy(block + RSX11_HOME_OFF_INDO, "[001,001]   ", 12u);
    memcpy(block + RSX11_HOME_OFF_INDF, "DECFILE11A  ", 12u);
    put_le16(block + RSX11_HOME_OFF_CHK1,
             checksum_words(block, RSX11_HOME_OFF_CHK1 / 2u));
    put_le16(block + RSX11_HOME_OFF_CHK2, header_checksum(block));
}

static int home_block_looks_valid(rsx11_image_t *img,
                                  const uint8_t block[RSX11_BLOCK_SIZE])
{
    static const uint8_t files11_id[12] = "DECFILE11A  ";
    uint16_t ibsz;
    uint32_t iblb;

    if (get_le16(block + RSX11_HOME_OFF_CHK1) !=
        checksum_words(block, RSX11_HOME_OFF_CHK1 / 2u)) {
        errno = EINVAL;
        return 0;
    }
    if (get_le16(block + RSX11_HOME_OFF_CHK2) != header_checksum(block)) {
        errno = EINVAL;
        return 0;
    }
    ibsz = get_le16(block + RSX11_HOME_OFF_IBSZ);
    iblb = get_hiword32(block + RSX11_HOME_OFF_IBLB);
    if (ibsz == 0u || iblb == 0u ||
        get_le16(block + RSX11_HOME_OFF_VLEV) != 0401u) {
        errno = EINVAL;
        return 0;
    }
    if (memcmp(block + RSX11_HOME_OFF_INDF, files11_id, sizeof(files11_id)) != 0) {
        errno = EINVAL;
        return 0;
    }
    if ((uint64_t)iblb * RSX11_BLOCK_SIZE >= img->size_bytes ||
        (uint64_t)ibsz * RSX11_BLOCK_SIZE > img->size_bytes ||
        ((uint64_t)iblb + (uint64_t)ibsz) * RSX11_BLOCK_SIZE > img->size_bytes) {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

static int read_home_candidate(rsx11_image_t *img, uint32_t lbn,
                               uint8_t block[RSX11_BLOCK_SIZE])
{
    if ((uint64_t)(lbn + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    return read_at(img->fp, (uint64_t)lbn * RSX11_BLOCK_SIZE,
                   block, RSX11_BLOCK_SIZE);
}

static void init_storage_bitmap(uint8_t *data, size_t data_size,
                                uint32_t total_blocks)
{
    uint8_t bitmap_blocks;

    memset(data, 0, data_size);
    if (data_size < RSX11_BLOCK_SIZE * 2u) {
        return;
    }

    bitmap_blocks = (uint8_t)((total_blocks + 4095u) / 4096u);
    data[3] = bitmap_blocks;
    memset(data + RSX11_BLOCK_SIZE, 0xffu, data_size - RSX11_BLOCK_SIZE);
}

static void update_storage_bitmap_control(uint8_t *data, size_t data_size,
                                          uint32_t total_blocks)
{
    uint8_t bitmap_blocks;
    size_t unit_off;
    uint32_t start;
    size_t i;

    if (data_size < RSX11_BLOCK_SIZE) {
        return;
    }
    bitmap_blocks = data[3];
    unit_off = 4u + (size_t)bitmap_blocks * 4u;
    if (unit_off + 4u > RSX11_BLOCK_SIZE) {
        return;
    }

    for (i = 0; i < bitmap_blocks; i++) {
        uint32_t end;
        uint32_t count = 0;
        uint32_t blockno;

        start = (uint32_t)i * 4096u;
        end = start + 4096u;
        if (end > total_blocks) {
            end = total_blocks;
        }
        for (blockno = start; blockno < end; blockno++) {
            if (storage_bitmap_is_free(data, data_size, blockno)) {
                count++;
            }
        }
        put_le16(data + 4u + i * 4u, (uint16_t)count);
        put_le16(data + 4u + i * 4u + 2u, 0u);
    }
    put_hiword32(data + unit_off, total_blocks);
}

static int build_dir_record(uint8_t rec[16], const rsx11_fid_t *fid,
                            const char *name, const char *ext,
                            uint16_t version)
{
    char extbuf[4];
    uint16_t extword;
    size_t ext_len = strlen(ext);

    if (ext_len > 3u) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memset(rec, 0, 16u);
    put_le16(rec + 0u, fid->num);
    put_le16(rec + 2u, fid->seq);
    put_le16(rec + 4u, fid->rvn);
    if (rad50_encode_name3(name, strlen(name), rec + 6u) != 0) {
        return -1;
    }

    memset(extbuf, ' ', sizeof(extbuf));
    memcpy(extbuf, ext, ext_len);
    if (rad50_encode_word(extbuf, &extword) != 0) {
        return -1;
    }
    put_le16(rec + 12u, extword);
    put_le16(rec + 14u, version);
    return 0;
}

static void update_header_size_fields(uint8_t block[RSX11_BLOCK_SIZE],
                                      uint32_t alloc_blocks,
                                      uint64_t size_bytes)
{
    uint32_t efbk;
    uint16_t ffby;

    efbk = (uint32_t)((size_bytes + RSX11_BLOCK_SIZE - 1u) / RSX11_BLOCK_SIZE);
    ffby = (uint16_t)(size_bytes % RSX11_BLOCK_SIZE);
    if (efbk != 0u && ffby == 0u) {
        ffby = RSX11_BLOCK_SIZE;
    }

    put_hiword32(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_HIBK,
                 alloc_blocks);
    put_hiword32(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_EFBK, efbk);
    put_le16(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_FFBY, ffby);
    put_le16(block + 510u, header_checksum(block));
}

static int find_free_file_number(const rsx11_home_t *home,
                                 const uint8_t *bitmap, size_t bitmap_size,
                                 uint16_t *fnum_out)
{
    uint16_t fnum;

    for (fnum = 1u; fnum <= home->fmax; fnum++) {
        if (!index_bitmap_test(bitmap, bitmap_size, fnum)) {
            *fnum_out = fnum;
            return 0;
        }
    }

    errno = ENOSPC;
    return -1;
}

static int find_directory_header(rsx11_image_t *img, rsx11_header_cache_t *cache,
                                 const char *dir,
                                 rsx11_file_header_t *dir_hdr_out,
                                 rsx11_dirlist_t *entries_out)
{
    rsx11_fid_t mfd_fid;
    rsx11_file_header_t mfd_hdr;
    rsx11_dirlist_t mfd_entries;
    uint16_t uic;
    char dir_name[7];
    size_t i;
    int found = 0;

    memset(&mfd_fid, 0, sizeof(mfd_fid));
    memset(&mfd_entries, 0, sizeof(mfd_entries));
    mfd_fid.num = 4u;
    mfd_fid.seq = 4u;
    if (lookup_header(img, cache, &mfd_fid, &mfd_hdr) != 0) {
        return -1;
    }

    if (strcmp(dir, "MFD") == 0) {
        *dir_hdr_out = mfd_hdr;
        if (entries_out != NULL) {
            if (read_dir_file(img, &mfd_hdr, cache, "MFD", entries_out) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (parse_uic_word(dir, &uic) != 0) {
        return -1;
    }
    snprintf(dir_name, sizeof(dir_name), "%03o%03o",
             (unsigned)((uic >> 8) & 0377u),
             (unsigned)(uic & 0377u));

    if (read_dir_file(img, &mfd_hdr, cache, "MFD", &mfd_entries) != 0) {
        return -1;
    }

    for (i = 0; i < mfd_entries.count; i++) {
        if (strcmp(mfd_entries.entries[i].ext, "DIR") == 0 &&
            strcmp(mfd_entries.entries[i].name, dir_name) == 0) {
            if (lookup_header(img, cache, &mfd_entries.entries[i].fid,
                              dir_hdr_out) != 0) {
                rsx11_free_dirlist(&mfd_entries);
                return -1;
            }
            found = 1;
            break;
        }
    }

    if (!found) {
        rsx11_free_dirlist(&mfd_entries);
        errno = ENOENT;
        return -1;
    }

    if (entries_out != NULL &&
        read_dir_file(img, dir_hdr_out, cache, dir, entries_out) != 0) {
        rsx11_free_dirlist(&mfd_entries);
        return -1;
    }

    rsx11_free_dirlist(&mfd_entries);
    return 0;
}

static void format_dir_name_from_uic(uint16_t uic, char out[7])
{
    snprintf(out, 7u, "%03o%03o",
             (unsigned)((uic >> 8) & 0377u),
             (unsigned)(uic & 0377u));
}

static int read_directory_storage(rsx11_image_t *img,
                                  const rsx11_file_header_t *hdr,
                                  uint8_t **data_out,
                                  size_t *alloc_bytes_out,
                                  uint64_t *logical_bytes_out)
{
    size_t alloc_bytes;
    uint64_t logical_bytes;
    uint8_t *data;

    alloc_bytes = (size_t)header_total_blocks(hdr) * RSX11_BLOCK_SIZE;
    logical_bytes = hdr->size_bytes != 0u
                        ? hdr->size_bytes
                        : (uint64_t)alloc_bytes;
    if (logical_bytes > alloc_bytes || (logical_bytes % 16u) != 0u) {
        errno = EINVAL;
        return -1;
    }

    data = malloc(alloc_bytes);
    if (data == NULL) {
        return -1;
    }
    if (read_header_data(img, hdr, data, alloc_bytes) != 0) {
        free(data);
        return -1;
    }

    *data_out = data;
    if (alloc_bytes_out != NULL) {
        *alloc_bytes_out = alloc_bytes;
    }
    if (logical_bytes_out != NULL) {
        *logical_bytes_out = logical_bytes;
    }
    return 0;
}

static int find_directory_slot(const uint8_t *dir_data, size_t alloc_bytes,
                               uint64_t logical_bytes, size_t *slot_out,
                               int *append_out)
{
    size_t slot;

    for (slot = 0; slot < (size_t)logical_bytes; slot += 16u) {
        if (get_le16(dir_data + slot) == 0u) {
            *slot_out = slot;
            *append_out = 0;
            return 0;
        }
    }
    if (logical_bytes + 16u > alloc_bytes) {
        errno = ENOSPC;
        return -1;
    }

    *slot_out = (size_t)logical_bytes;
    *append_out = 1;
    return 0;
}

static int load_allocation_state(rsx11_image_t *img, rsx11_header_cache_t *cache,
                                 const rsx11_home_t *home,
                                 uint8_t **index_bitmap_out,
                                 size_t *index_size_out,
                                 uint8_t **storage_bitmap_out,
                                 size_t *storage_size_out,
                                 rsx11_file_header_t *bitmap_hdr_out,
                                 uint32_t *unit_blocks_out)
{
    if (load_index_bitmap(img, home, index_bitmap_out, index_size_out) != 0) {
        return -1;
    }
    if (load_storage_bitmap(img, cache,
                            storage_bitmap_out, storage_size_out,
                            bitmap_hdr_out, unit_blocks_out) != 0) {
        free(*index_bitmap_out);
        *index_bitmap_out = NULL;
        return -1;
    }
    return 0;
}

static int is_directory_empty(const uint8_t *dir_data, uint64_t logical_bytes)
{
    size_t off;

    for (off = 0; off < (size_t)logical_bytes; off += 16u) {
        if (get_le16(dir_data + off) != 0u) {
            return 0;
        }
    }
    return 1;
}

static int build_directory_header(uint8_t block[RSX11_BLOCK_SIZE],
                                  uint16_t fnum, uint16_t seq,
                                  uint16_t uic, const char *dir_name,
                                  uint32_t start_lbn)
{
    if (build_file_header(block, fnum, seq, uic, 0160000u,
                          dir_name, "DIR", 1u,
                          start_lbn, 1u, RSX11_BLOCK_SIZE) != 0) {
        return -1;
    }
    block[RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_RATT] = 1u;
    block[RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_RTYP] = 0u;
    put_le16(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_RSIZ, 16u);
    put_hiword32(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_EFBK, 2u);
    put_le16(block + RSX11_HDR_OFF_UFAT + RSX11_FCS_OFF_FFBY, 0u);
    put_le16(block + 510u, header_checksum(block));
    return 0;
}

static int open_image_mode(rsx11_image_t *img, const char *path,
                           const char *mode)
{
    long end;

    memset(img, 0, sizeof(*img));
    img->fp = fopen(path, mode);
    if (img->fp == NULL) {
        return -1;
    }
    if (fseek(img->fp, 0, SEEK_END) != 0) {
        rsx11_close_image(img);
        return -1;
    }
    end = ftell(img->fp);
    if (end < 0) {
        rsx11_close_image(img);
        return -1;
    }
    img->size_bytes = (uint64_t)end;
    img->total_blocks = (uint32_t)(img->size_bytes / RSX11_BLOCK_SIZE);
    if (seek_to(img->fp, 0) != 0) {
        rsx11_close_image(img);
        return -1;
    }
    return 0;
}

int rsx11_open_image(rsx11_image_t *img, const char *path)
{
    return open_image_mode(img, path, "rb");
}

int rsx11_open_image_rw(rsx11_image_t *img, const char *path)
{
    return open_image_mode(img, path, "rb+");
}

void rsx11_close_image(rsx11_image_t *img)
{
    if (img->fp != NULL) {
        fclose(img->fp);
        img->fp = NULL;
    }
    img->size_bytes = 0;
    img->total_blocks = 0;
}

int rsx11_read_boot_block(rsx11_image_t *img,
                          uint8_t block[RSX11_BLOCK_SIZE])
{
    if (img == NULL || block == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (img->size_bytes < RSX11_BLOCK_SIZE) {
        errno = EIO;
        return -1;
    }
    return read_at(img->fp, 0u, block, RSX11_BLOCK_SIZE);
}

int rsx11_write_boot_block(rsx11_image_t *img,
                           const uint8_t block[RSX11_BLOCK_SIZE])
{
    if (img == NULL || block == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (img->size_bytes < RSX11_BLOCK_SIZE) {
        errno = EIO;
        return -1;
    }
    if (write_at(img->fp, 0u, block, RSX11_BLOCK_SIZE) != 0) {
        return -1;
    }
    if (fflush(img->fp) != 0) {
        return -1;
    }
    return 0;
}

int rsx11_read_home(rsx11_image_t *img, rsx11_home_t *home)
{
    uint8_t block[RSX11_BLOCK_SIZE];
    uint32_t lbn;
    int found = 0;

    if (read_home_candidate(img, RSX11_HOME_BLOCK, block) == 0 &&
        home_block_looks_valid(img, block)) {
        found = 1;
    } else {
        for (lbn = 256u; lbn < img->total_blocks; lbn += 256u) {
            if (read_home_candidate(img, lbn, block) != 0) {
                return -1;
            }
            if (home_block_looks_valid(img, block)) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        errno = EINVAL;
        return -1;
    }

    memset(home, 0, sizeof(*home));
    home->ibsz = get_le16(block + RSX11_HOME_OFF_IBSZ);
    home->iblb = get_hiword32(block + RSX11_HOME_OFF_IBLB);
    home->fmax = get_le16(block + RSX11_HOME_OFF_FMAX);
    home->sbcl = get_le16(block + RSX11_HOME_OFF_SBCL);
    home->dvty = get_le16(block + RSX11_HOME_OFF_DVTY);
    home->vlev = get_le16(block + RSX11_HOME_OFF_VLEV);
    copy_trim(home->vnam, sizeof(home->vnam),
              block + RSX11_HOME_OFF_VNAM, 12u);
    home->vown = get_le16(block + RSX11_HOME_OFF_VOWN);
    home->vpro = get_le16(block + RSX11_HOME_OFF_VPRO);
    home->vcha = get_le16(block + RSX11_HOME_OFF_VCHA);
    home->dfpr = get_le16(block + RSX11_HOME_OFF_DFPR);
    home->wisz = block[RSX11_HOME_OFF_WISZ];
    home->fiex = block[RSX11_HOME_OFF_FIEX];
    home->lruc = block[RSX11_HOME_OFF_LRUC];
    copy_trim(home->revd, sizeof(home->revd),
              block + RSX11_HOME_OFF_REVD, 7u);
    copy_trim(home->vdat, sizeof(home->vdat),
              block + RSX11_HOME_OFF_VDAT, 14u);
    return 0;
}

int rsx11_read_directory(rsx11_image_t *img, rsx11_dirlist_t *out)
{
    rsx11_header_cache_t cache;
    rsx11_fid_t mfd_fid;
    rsx11_file_header_t mfd_hdr;
    rsx11_dirlist_t mfd_entries;
    size_t i;

    memset(out, 0, sizeof(*out));
    memset(&cache, 0, sizeof(cache));
    memset(&mfd_fid, 0, sizeof(mfd_fid));
    mfd_fid.num = 4u;
    mfd_fid.seq = 4u;
    if (lookup_header(img, &cache, &mfd_fid, &mfd_hdr) != 0) {
        free_header_cache(&cache);
        return -1;
    }

    memset(&mfd_entries, 0, sizeof(mfd_entries));
    if (read_dir_file(img, &mfd_hdr, &cache, "MFD", &mfd_entries) != 0) {
        free_header_cache(&cache);
        rsx11_free_dirlist(&mfd_entries);
        return -1;
    }

    for (i = 0; i < mfd_entries.count; i++) {
        if (append_dirent(out, &mfd_entries.entries[i]) != 0) {
            free_header_cache(&cache);
            rsx11_free_dirlist(&mfd_entries);
            rsx11_free_dirlist(out);
            return -1;
        }
    }

    for (i = 0; i < mfd_entries.count; i++) {
        rsx11_file_header_t dir_hdr;
        char uic[16];

        if (strcmp(mfd_entries.entries[i].ext, "DIR") != 0 ||
            strcmp(mfd_entries.entries[i].name, "000000") == 0 ||
            !format_uic(mfd_entries.entries[i].name, uic, sizeof(uic))) {
            continue;
        }

        if (lookup_header(img, &cache, &mfd_entries.entries[i].fid,
                          &dir_hdr) != 0) {
            continue;
        }
        if (read_dir_file(img, &dir_hdr, &cache, uic, out) != 0) {
            free_header_cache(&cache);
            rsx11_free_dirlist(&mfd_entries);
            rsx11_free_dirlist(out);
            return -1;
        }
    }

    free_header_cache(&cache);
    rsx11_free_dirlist(&mfd_entries);
    return 0;
}

int rsx11_extract_selected(rsx11_image_t *img,
                           const rsx11_dirent_t *entries, size_t count,
                           const char *outdir, int preserve_dirs,
                           unsigned *files_out)
{
    rsx11_header_cache_t cache;
    unsigned written = 0;
    size_t i;

    if (files_out != NULL) {
        *files_out = 0;
    }
    if (mkdir_parents(outdir) != 0) {
        return -1;
    }
    memset(&cache, 0, sizeof(cache));

    for (i = 0; i < count; i++) {
        char rel_dir[64];
        char file_name[32];
        char full_path[4096];
        rsx11_file_header_t hdr;

        if (strcmp(entries[i].ext, "DIR") == 0) {
            continue;
        }

        if (lookup_header(img, &cache, &entries[i].fid, &hdr) != 0) {
            continue;
        }

        format_host_file_name(&entries[i], file_name, sizeof(file_name));
        if (preserve_dirs) {
            host_dir_name(entries[i].dir, rel_dir, sizeof(rel_dir));
            snprintf(full_path, sizeof(full_path), "%s/%s", outdir, rel_dir);
            if (mkdir_parents(full_path) != 0) {
                free_header_cache(&cache);
                return -1;
            }
            snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                     outdir, rel_dir, file_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", outdir, file_name);
        }

        if (extract_header_to_path(img, &hdr, full_path) != 0) {
            free_header_cache(&cache);
            return -1;
        }
        written++;
    }

    free_header_cache(&cache);
    if (files_out != NULL) {
        *files_out = written;
    }
    return 0;
}

int rsx11_remove_entry(rsx11_image_t *img, const rsx11_dirent_t *ent)
{
    rsx11_home_t home;
    rsx11_dirlist_t list;
    rsx11_file_header_t hdr;
    rsx11_header_cache_t cache;
    rsx11_file_header_t bitmap_hdr;
    uint8_t rec[16];
    uint8_t zero[16];
    uint8_t *storage_bitmap = NULL;
    uint8_t *index_bitmap = NULL;
    size_t storage_size = 0;
    size_t index_size = 0;
    uint32_t unit_blocks = 0;
    size_t i;
    unsigned refs = 0;
    int last_link = 0;
    int rc = -1;

    if (ent == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(ent->ext, "DIR") == 0) {
        errno = EISDIR;
        return -1;
    }
    if (ent->raw_offset + sizeof(rec) > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    if (read_at(img->fp, ent->raw_offset, rec, sizeof(rec)) != 0) {
        return -1;
    }
    if (get_le16(rec + 0u) != ent->fid.num ||
        get_le16(rec + 2u) != ent->fid.seq ||
        get_le16(rec + 14u) != ent->version) {
        errno = ENOENT;
        return -1;
    }

    memset(&list, 0, sizeof(list));
    if (rsx11_read_directory(img, &list) != 0) {
        return -1;
    }
    for (i = 0; i < list.count; i++) {
        if (same_fid(&list.entries[i].fid, &ent->fid)) {
            refs++;
        }
    }
    if (refs == 0u) {
        rsx11_free_dirlist(&list);
        errno = ENOENT;
        return -1;
    }
    last_link = (refs == 1u);

    memset(&cache, 0, sizeof(cache));
    if (last_link) {
        if (rsx11_read_home(img, &home) != 0) {
            rsx11_free_dirlist(&list);
            return -1;
        }
        if (lookup_header(img, &cache, &ent->fid, &hdr) != 0) {
            rsx11_free_dirlist(&list);
            return -1;
        }
        if (load_storage_bitmap(img, &cache, &storage_bitmap, &storage_size,
                                &bitmap_hdr, &unit_blocks) != 0) {
            rsx11_free_dirlist(&list);
            free_header_cache(&cache);
            return -1;
        }
        if (load_index_bitmap(img, &home, &index_bitmap, &index_size) != 0) {
            rsx11_free_dirlist(&list);
            free(storage_bitmap);
            free_header_cache(&cache);
            return -1;
        }
        if (release_header_chain_allocation(&hdr,
                                            index_bitmap, index_size,
                                            storage_bitmap, storage_size,
                                            unit_blocks) != 0) {
            rsx11_free_dirlist(&list);
            free(index_bitmap);
            free(storage_bitmap);
            free_header_cache(&cache);
            return -1;
        }
    }

    memset(zero, 0, sizeof(zero));
    if (write_at(img->fp, ent->raw_offset, zero, sizeof(zero)) != 0) {
        goto out;
    }
    if (last_link) {
        if (write_header_data(img, &bitmap_hdr,
                              storage_bitmap, storage_size) != 0) {
            goto out;
        }
        if (write_at(img->fp, (uint64_t)home.iblb * RSX11_BLOCK_SIZE,
                     index_bitmap, index_size) != 0) {
            goto out;
        }
    }
    if (fflush(img->fp) != 0) {
        goto out;
    }

    rc = 0;

out:
    rsx11_free_dirlist(&list);
    free(index_bitmap);
    free(storage_bitmap);
    free_header_cache(&cache);
    return rc;
}

int rsx11_make_directory(rsx11_image_t *img, const char *dir)
{
    rsx11_home_t home;
    rsx11_header_cache_t cache;
    rsx11_file_header_t mfd_hdr;
    rsx11_file_header_t bitmap_hdr;
    rsx11_dirlist_t mfd_entries;
    uint8_t *mfd_data = NULL;
    uint8_t *index_bitmap = NULL;
    uint8_t *storage_bitmap = NULL;
    uint8_t new_dir_hdr_block[RSX11_BLOCK_SIZE];
    uint8_t mfd_hdr_block[RSX11_BLOCK_SIZE];
    uint8_t dir_rec[16];
    uint8_t zero_block[RSX11_BLOCK_SIZE];
    size_t mfd_alloc_bytes = 0;
    uint64_t mfd_logical_bytes = 0;
    size_t mfd_slot = 0;
    int append_slot = 0;
    size_t index_size = 0;
    size_t storage_size = 0;
    uint32_t unit_blocks = 0;
    uint32_t start_lbn = 0;
    uint16_t uic;
    uint16_t fnum = 0;
    uint16_t seq = 0;
    char dir_name[7];
    size_t i;
    int rc = -1;

    if (dir == NULL || strcmp(dir, "MFD") == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&cache, 0, sizeof(cache));
    memset(&mfd_entries, 0, sizeof(mfd_entries));
    if (parse_uic_word(dir, &uic) != 0) {
        return -1;
    }
    if (uic == 0u) {
        errno = EINVAL;
        return -1;
    }
    format_dir_name_from_uic(uic, dir_name);

    if (rsx11_read_home(img, &home) != 0) {
        goto out;
    }
    if (find_directory_header(img, &cache, "MFD", &mfd_hdr, &mfd_entries) != 0) {
        goto out;
    }
    for (i = 0; i < mfd_entries.count; i++) {
        if (strcmp(mfd_entries.entries[i].ext, "DIR") == 0 &&
            strcmp(mfd_entries.entries[i].name, dir_name) == 0) {
            errno = EEXIST;
            goto out;
        }
    }

    if (read_directory_storage(img, &mfd_hdr, &mfd_data,
                               &mfd_alloc_bytes, &mfd_logical_bytes) != 0) {
        goto out;
    }
    if (find_directory_slot(mfd_data, mfd_alloc_bytes, mfd_logical_bytes,
                            &mfd_slot, &append_slot) != 0) {
        goto out;
    }

    if (load_allocation_state(img, &cache, &home,
                              &index_bitmap, &index_size,
                              &storage_bitmap, &storage_size,
                              &bitmap_hdr, &unit_blocks) != 0) {
        goto out;
    }
    if (find_free_file_number(&home, index_bitmap, index_size, &fnum) != 0) {
        goto out;
    }
    if (read_slot_sequence(img, &home, fnum, &seq) != 0) {
        goto out;
    }
    seq = seq == 0xffffu ? 1u : (uint16_t)(seq + 1u);
    if (seq == 0u) {
        seq = 1u;
    }
    if (find_free_blocks(storage_bitmap, storage_size, unit_blocks,
                         1u, &start_lbn) != 0) {
        goto out;
    }
    if ((uint64_t)(start_lbn + 1u) * RSX11_BLOCK_SIZE > img->size_bytes) {
        errno = ENOSPC;
        goto out;
    }

    if (build_directory_header(new_dir_hdr_block, fnum, seq, uic,
                               dir_name, start_lbn) != 0) {
        goto out;
    }
    if (build_dir_record(dir_rec, &(rsx11_fid_t){ fnum, seq, 0u },
                         dir_name, "DIR", 1u) != 0) {
        goto out;
    }
    memcpy(mfd_data + mfd_slot, dir_rec, sizeof(dir_rec));

    if (append_slot) {
        if (read_at(img->fp, mfd_hdr.raw_offset,
                    mfd_hdr_block, sizeof(mfd_hdr_block)) != 0) {
            goto out;
        }
        mfd_logical_bytes += 16u;
        update_header_size_fields(mfd_hdr_block,
                                  header_total_blocks(&mfd_hdr),
                                  mfd_logical_bytes);
    }

    memset(zero_block, 0, sizeof(zero_block));
    if (write_at(img->fp, (uint64_t)start_lbn * RSX11_BLOCK_SIZE,
                 zero_block, sizeof(zero_block)) != 0) {
        goto out;
    }

    storage_bitmap_set_free(storage_bitmap, storage_size, start_lbn, 0);
    index_bitmap_set(index_bitmap, index_size, fnum, 1);

    if (write_header_slot(img, &home, fnum, new_dir_hdr_block) != 0) {
        goto out;
    }
    if (write_header_data(img, &bitmap_hdr,
                          storage_bitmap, storage_size) != 0) {
        goto out;
    }
    if (write_at(img->fp, (uint64_t)home.iblb * RSX11_BLOCK_SIZE,
                 index_bitmap, index_size) != 0) {
        goto out;
    }
    if (write_header_data(img, &mfd_hdr, mfd_data, mfd_alloc_bytes) != 0) {
        goto out;
    }
    if (append_slot &&
        write_at(img->fp, mfd_hdr.raw_offset,
                 mfd_hdr_block, sizeof(mfd_hdr_block)) != 0) {
        goto out;
    }
    if (fflush(img->fp) != 0) {
        goto out;
    }

    rc = 0;

out:
    free(storage_bitmap);
    free(index_bitmap);
    free(mfd_data);
    rsx11_free_dirlist(&mfd_entries);
    free_header_cache(&cache);
    return rc;
}

int rsx11_remove_directory(rsx11_image_t *img, const char *dir)
{
    rsx11_home_t home;
    rsx11_header_cache_t cache;
    rsx11_file_header_t mfd_hdr;
    rsx11_file_header_t dir_hdr;
    rsx11_file_header_t bitmap_hdr;
    rsx11_dirlist_t mfd_entries;
    rsx11_dirlist_t all_entries;
    uint8_t *dir_data = NULL;
    uint8_t *index_bitmap = NULL;
    uint8_t *storage_bitmap = NULL;
    size_t dir_alloc_bytes = 0;
    uint64_t dir_logical_bytes = 0;
    size_t index_size = 0;
    size_t storage_size = 0;
    uint32_t unit_blocks = 0;
    rsx11_dirent_t *target = NULL;
    uint16_t uic;
    char dir_name[7];
    size_t i;
    unsigned refs = 0;
    uint8_t zero[16];
    int rc = -1;

    if (dir == NULL || strcmp(dir, "MFD") == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&cache, 0, sizeof(cache));
    memset(&mfd_entries, 0, sizeof(mfd_entries));
    memset(&all_entries, 0, sizeof(all_entries));
    if (parse_uic_word(dir, &uic) != 0) {
        return -1;
    }
    if (uic == 0u) {
        errno = EINVAL;
        return -1;
    }
    format_dir_name_from_uic(uic, dir_name);

    if (rsx11_read_home(img, &home) != 0) {
        goto out;
    }
    if (find_directory_header(img, &cache, "MFD", &mfd_hdr, &mfd_entries) != 0) {
        goto out;
    }
    for (i = 0; i < mfd_entries.count; i++) {
        if (strcmp(mfd_entries.entries[i].ext, "DIR") == 0 &&
            strcmp(mfd_entries.entries[i].name, dir_name) == 0) {
            target = &mfd_entries.entries[i];
            break;
        }
    }
    if (target == NULL) {
        errno = ENOENT;
        goto out;
    }
    if (lookup_header(img, &cache, &target->fid, &dir_hdr) != 0) {
        goto out;
    }
    if (read_directory_storage(img, &dir_hdr, &dir_data,
                               &dir_alloc_bytes, &dir_logical_bytes) != 0) {
        goto out;
    }
    if (!is_directory_empty(dir_data, dir_logical_bytes)) {
        errno = ENOTEMPTY;
        goto out;
    }

    if (rsx11_read_directory(img, &all_entries) != 0) {
        goto out;
    }
    for (i = 0; i < all_entries.count; i++) {
        if (same_fid(&all_entries.entries[i].fid, &target->fid)) {
            refs++;
        }
    }
    if (refs != 1u) {
        errno = EBUSY;
        goto out;
    }

    if (load_allocation_state(img, &cache, &home,
                              &index_bitmap, &index_size,
                              &storage_bitmap, &storage_size,
                              &bitmap_hdr, &unit_blocks) != 0) {
        goto out;
    }
    if (release_header_chain_allocation(&dir_hdr,
                                        index_bitmap, index_size,
                                        storage_bitmap, storage_size,
                                        unit_blocks) != 0) {
        goto out;
    }

    memset(zero, 0, sizeof(zero));
    if (write_at(img->fp, target->raw_offset, zero, sizeof(zero)) != 0) {
        goto out;
    }
    if (write_header_data(img, &bitmap_hdr,
                          storage_bitmap, storage_size) != 0) {
        goto out;
    }
    if (write_at(img->fp, (uint64_t)home.iblb * RSX11_BLOCK_SIZE,
                 index_bitmap, index_size) != 0) {
        goto out;
    }
    if (fflush(img->fp) != 0) {
        goto out;
    }

    rc = 0;

out:
    free(storage_bitmap);
    free(index_bitmap);
    free(dir_data);
    rsx11_free_dirlist(&all_entries);
    rsx11_free_dirlist(&mfd_entries);
    free_header_cache(&cache);
    return rc;
}

int rsx11_add_file(rsx11_image_t *img, const char *host_path,
                   const char *dir, const char *name, const char *ext,
                   int have_version, uint16_t version,
                   rsx11_dirent_t *out)
{
    rsx11_home_t home;
    rsx11_header_cache_t cache;
    rsx11_file_header_t dir_hdr;
    rsx11_file_header_t bitmap_hdr;
    rsx11_dirlist_t dir_entries;
    uint8_t *dir_data = NULL;
    uint8_t *storage_bitmap = NULL;
    uint8_t *index_bitmap = NULL;
    uint8_t dir_hdr_block[RSX11_BLOCK_SIZE];
    uint8_t dir_rec[16];
    size_t dir_alloc_bytes;
    uint64_t dir_logical_bytes;
    size_t dir_slot = 0;
    int append_slot = 0;
    size_t storage_size = 0;
    size_t index_size = 0;
    uint32_t unit_blocks = 0;
    uint32_t blocks;
    rsx11_extent_t extents[RSX11_MAX_EXTENTS];
    size_t extent_count = 0;
    size_t header_count = 1u;
    uint16_t header_fnums[RSX11_MAX_HEADER_SEGMENTS];
    uint16_t header_seqs[RSX11_MAX_HEADER_SEGMENTS];
    uint8_t header_blocks[RSX11_MAX_HEADER_SEGMENTS][RSX11_BLOCK_SIZE];
    uint16_t fnum = 0;
    uint16_t seq = 0;
    uint16_t owner_uic;
    uint16_t version_out = version;
    uint64_t file_bytes;
    FILE *host = NULL;
    size_t i;
    int rc = -1;

    memset(&cache, 0, sizeof(cache));
    memset(&dir_entries, 0, sizeof(dir_entries));
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }

    if (host_path == NULL || dir == NULL || name == NULL || ext == NULL ||
        name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    host = fopen(host_path, "rb");
    if (host == NULL) {
        return -1;
    }
    if (seek_to(host, 0) != 0) {
        goto out;
    }
    if (fseeko(host, 0, SEEK_END) != 0) {
        goto out;
    }
    {
        off_t end = ftello(host);

        if (end < 0) {
            goto out;
        }
        file_bytes = (uint64_t)end;
    }
    if (seek_to(host, 0) != 0) {
        goto out;
    }
    blocks = (uint32_t)((file_bytes + RSX11_BLOCK_SIZE - 1u) /
                        RSX11_BLOCK_SIZE);

    if (rsx11_read_home(img, &home) != 0) {
        goto out;
    }
    if (strcmp(dir, "MFD") == 0) {
        owner_uic = home.vown;
    } else if (parse_uic_word(dir, &owner_uic) != 0) {
        goto out;
    }

    if (find_directory_header(img, &cache, dir, &dir_hdr, &dir_entries) != 0) {
        goto out;
    }

    for (i = 0; i < dir_entries.count; i++) {
        if (strcmp(dir_entries.entries[i].name, name) == 0 &&
            strcmp(dir_entries.entries[i].ext, ext) == 0) {
            if (have_version &&
                dir_entries.entries[i].version == version_out) {
                errno = EEXIST;
                goto out;
            }
            if (!have_version &&
                dir_entries.entries[i].version >= version_out) {
                if (dir_entries.entries[i].version == 0xffffu) {
                    errno = EOVERFLOW;
                    goto out;
                }
                version_out = (uint16_t)(dir_entries.entries[i].version + 1u);
            }
        }
    }
    if (!have_version) {
        if (version_out == 0u) {
            version_out = 1u;
        }
    } else if (version_out == 0u) {
        errno = EINVAL;
        goto out;
    }

    dir_alloc_bytes = (size_t)header_total_blocks(&dir_hdr) * RSX11_BLOCK_SIZE;
    dir_logical_bytes = dir_hdr.size_bytes != 0u
                            ? dir_hdr.size_bytes
                            : (uint64_t)dir_alloc_bytes;
    if (dir_logical_bytes > dir_alloc_bytes ||
        (dir_logical_bytes % 16u) != 0u) {
        errno = EINVAL;
        goto out;
    }

    dir_data = malloc(dir_alloc_bytes);
    if (dir_data == NULL) {
        goto out;
    }
    if (read_header_data(img, &dir_hdr, dir_data, dir_alloc_bytes) != 0) {
        goto out;
    }

    for (dir_slot = 0; dir_slot < (size_t)dir_logical_bytes; dir_slot += 16u) {
        if (get_le16(dir_data + dir_slot) == 0u) {
            break;
        }
    }
    if (dir_slot >= (size_t)dir_logical_bytes) {
        if (dir_logical_bytes + 16u > dir_alloc_bytes) {
            errno = ENOSPC;
            goto out;
        }
        dir_slot = (size_t)dir_logical_bytes;
        append_slot = 1;
    }

    if (load_index_bitmap(img, &home, &index_bitmap, &index_size) != 0) {
        goto out;
    }
    if (find_free_file_number(&home, index_bitmap, index_size, &fnum) != 0) {
        goto out;
    }
    if (read_slot_sequence(img, &home, fnum, &seq) != 0) {
        goto out;
    }
    seq = seq == 0xffffu ? 1u : (uint16_t)(seq + 1u);
    if (seq == 0u) {
        seq = 1u;
    }

    if (load_storage_bitmap(img, &cache, &storage_bitmap, &storage_size,
                            &bitmap_hdr, &unit_blocks) != 0) {
        goto out;
    }
    if (blocks != 0u &&
        allocate_file_extents(storage_bitmap, storage_size, unit_blocks,
                              blocks, extents, &extent_count) != 0) {
        goto out;
    }
    if (header_segment_count_for_extents(extents, extent_count,
                                         &header_count) != 0) {
        goto out;
    }

    for (i = 0; i < header_count; i++) {
        if (find_free_file_number(&home, index_bitmap, index_size, &fnum) != 0) {
            goto out;
        }
        if (read_slot_sequence(img, &home, fnum, &seq) != 0) {
            goto out;
        }
        seq = seq == 0xffffu ? 1u : (uint16_t)(seq + 1u);
        if (seq == 0u) {
            seq = 1u;
        }
        header_fnums[i] = fnum;
        header_seqs[i] = seq;
        index_bitmap_set(index_bitmap, index_size, fnum, 1);
    }

    for (i = 0; i < extent_count; i++) {
        uint32_t lbn = extents[i].lbn;
        uint32_t count = extents[i].blocks;
        uint32_t b;

        if (count == 0u ||
            (uint64_t)(lbn + count) * RSX11_BLOCK_SIZE > img->size_bytes) {
            errno = ENOSPC;
            goto out;
        }
        for (b = 0; b < count; b++) {
            storage_bitmap_set_free(storage_bitmap, storage_size, lbn + b, 0);
        }
    }

    for (i = 0; i < header_count; i++) {
        size_t first_extent = i * (RSX11_MAP_MAX_WORDS / 2u);
        size_t seg_extents = extent_count > first_extent
                                 ? extent_count - first_extent
                                 : 0u;
        rsx11_fid_t next_fid = { 0u, 0u, 0u };

        if (seg_extents > (RSX11_MAP_MAX_WORDS / 2u)) {
            seg_extents = RSX11_MAP_MAX_WORDS / 2u;
        }
        if (i + 1u < header_count) {
            next_fid.num = header_fnums[i + 1u];
            next_fid.seq = header_seqs[i + 1u];
        }
        if (build_file_header_chain_segment(
                header_blocks[i],
                header_fnums[i], header_seqs[i],
                owner_uic, home.dfpr,
                name, ext, version_out,
                extents + first_extent, seg_extents,
                i == 0u ? file_bytes : 0u,
                (uint8_t)i,
                i + 1u < header_count ? &next_fid : NULL) != 0) {
            goto out;
        }
    }
    if (build_dir_record(dir_rec,
                         &(rsx11_fid_t){ header_fnums[0], header_seqs[0], 0u },
                         name, ext, version_out) != 0) {
        goto out;
    }
    memcpy(dir_data + dir_slot, dir_rec, sizeof(dir_rec));

    if (append_slot) {
        if (read_at(img->fp, dir_hdr.raw_offset,
                    dir_hdr_block, sizeof(dir_hdr_block)) != 0) {
            goto out;
        }
        dir_logical_bytes += 16u;
        update_header_size_fields(dir_hdr_block,
                                  header_total_blocks(&dir_hdr),
                                  dir_logical_bytes);
    }

    for (i = 0; i < extent_count; i++) {
        uint32_t lbn = extents[i].lbn;
        uint32_t count = extents[i].blocks;
        uint32_t b;

        for (b = 0; b < count; b++) {
            uint8_t block[RSX11_BLOCK_SIZE];
            size_t got = fread(block, 1, sizeof(block), host);

            if (got < sizeof(block)) {
                if (ferror(host)) {
                    errno = EIO;
                    goto out;
                }
                memset(block + got, 0, sizeof(block) - got);
            }
            if (write_at(img->fp,
                         (uint64_t)(lbn + b) * RSX11_BLOCK_SIZE,
                         block, sizeof(block)) != 0) {
                goto out;
            }
        }
    }
    if (fgetc(host) != EOF) {
        errno = EIO;
        goto out;
    }

    for (i = 0; i < header_count; i++) {
        if (write_header_slot(img, &home,
                              header_fnums[i], header_blocks[i]) != 0) {
            goto out;
        }
    }
    if (write_header_data(img, &bitmap_hdr,
                          storage_bitmap, storage_size) != 0) {
        goto out;
    }
    if (write_at(img->fp, (uint64_t)home.iblb * RSX11_BLOCK_SIZE,
                 index_bitmap, index_size) != 0) {
        goto out;
    }
    if (write_header_data(img, &dir_hdr, dir_data, dir_alloc_bytes) != 0) {
        goto out;
    }
    if (append_slot &&
        write_at(img->fp, dir_hdr.raw_offset,
                 dir_hdr_block, sizeof(dir_hdr_block)) != 0) {
        goto out;
    }
    if (fflush(img->fp) != 0) {
        goto out;
    }

    if (out != NULL) {
        strncpy(out->dir, dir, sizeof(out->dir) - 1u);
        strncpy(out->name, name, sizeof(out->name) - 1u);
        strncpy(out->ext, ext, sizeof(out->ext) - 1u);
        out->version = version_out;
        out->fid.num = header_fnums[0];
        out->fid.seq = header_seqs[0];
        out->fid.rvn = 0u;
        out->blocks = blocks;
        out->raw_offset = 0u;
    }
    rc = 0;

out:
    if (host != NULL) {
        fclose(host);
    }
    free(index_bitmap);
    free(storage_bitmap);
    free(dir_data);
    rsx11_free_dirlist(&dir_entries);
    free_header_cache(&cache);
    return rc;
}

static int default_fmax_for_blocks(uint32_t total_blocks, uint16_t *fmax_out)
{
    uint32_t fmax;

    fmax = total_blocks / 16u;
    if (fmax < 16u) {
        fmax = 16u;
    }
    if (fmax > 1024u) {
        fmax = 1024u;
    }
    *fmax_out = (uint16_t)fmax;
    return 0;
}

int rsx11_mkfs(const char *path, uint32_t blocks, const char *label)
{
    static const uint32_t badblk_blocks = 20u;
    rsx11_image_t img;
    rsx11_extent_t index_extents[2];
    rsx11_mkfs_file_t files[5];
    uint8_t home_block[RSX11_BLOCK_SIZE];
    uint8_t header_block[RSX11_BLOCK_SIZE];
    uint8_t mfd_block[RSX11_BLOCK_SIZE];
    uint8_t *storage_bitmap = NULL;
    uint8_t *index_bitmap = NULL;
    const char *volume_label;
    uint32_t iblb;
    uint32_t bitmap_data_blocks;
    uint32_t bitmap_total_blocks;
    uint32_t sys_data_blocks;
    uint32_t data_start;
    uint32_t unit_blocks;
    uint32_t min_blocks;
    size_t storage_size;
    size_t index_size;
    uint16_t fmax;
    size_t i;
    int rc = -1;

    if (path == NULL || path[0] == '\0' || blocks == 0u) {
        errno = EINVAL;
        return -1;
    }
    volume_label = (label != NULL && label[0] != '\0') ? label : "RSXVOL";
    if (strlen(volume_label) > 12u) {
        errno = ENAMETOOLONG;
        return -1;
    }
    bitmap_data_blocks = (blocks + 4095u) / 4096u;
    if (bitmap_data_blocks == 0u || bitmap_data_blocks > 255u) {
        errno = EFBIG;
        return -1;
    }
    bitmap_total_blocks = 1u + bitmap_data_blocks;
    if (default_fmax_for_blocks(blocks, &fmax) != 0) {
        return -1;
    }

    iblb = (blocks + 1u) / 2u;
    sys_data_blocks = bitmap_total_blocks + badblk_blocks + 1u;
    min_blocks = iblb + 1u + (uint32_t)fmax + sys_data_blocks + 16u;
    if (blocks < min_blocks) {
        errno = ENOSPC;
        return -1;
    }

    memset(&img, 0, sizeof(img));
    img.fp = fopen(path, "wb+");
    if (img.fp == NULL) {
        return -1;
    }
    img.size_bytes = (uint64_t)blocks * RSX11_BLOCK_SIZE;
    img.total_blocks = blocks;
    if (img.size_bytes != 0u) {
        if (seek_to(img.fp, img.size_bytes - 1u) != 0) {
            goto out;
        }
        if (fputc(0, img.fp) == EOF) {
            errno = EIO;
            goto out;
        }
        if (fflush(img.fp) != 0) {
            goto out;
        }
    }

    build_home_block(home_block, 1u, iblb, fmax, volume_label);
    if (write_at(img.fp, (uint64_t)RSX11_HOME_BLOCK * RSX11_BLOCK_SIZE,
                 home_block, sizeof(home_block)) != 0) {
        goto out;
    }

    data_start = iblb + 1u + (uint32_t)fmax;
    files[0] = (rsx11_mkfs_file_t){
        "INDEXF", "SYS", { 1u, 1u, 0u }, 0u, 2u + 1u + (uint32_t)fmax
    };
    files[1] = (rsx11_mkfs_file_t){
        "BITMAP", "SYS", { 2u, 2u, 0u }, data_start + 1u, bitmap_total_blocks
    };
    files[2] = (rsx11_mkfs_file_t){
        "BADBLK", "SYS", { 3u, 3u, 0u },
        blocks - badblk_blocks, badblk_blocks
    };
    files[3] = (rsx11_mkfs_file_t){
        "000000", "DIR", { 4u, 4u, 0u },
        data_start, 1u
    };
    files[4] = (rsx11_mkfs_file_t){
        "CORIMG", "SYS", { 5u, 5u, 0u }, 0u, 0u
    };
    unit_blocks = blocks;

    index_extents[0].lbn = 0u;
    index_extents[0].blocks = 2u;
    index_extents[1].lbn = iblb;
    index_extents[1].blocks = (uint16_t)(1u + (uint32_t)fmax);
    if (build_file_header_extents(header_block,
                                  files[0].fid.num, files[0].fid.seq,
                                  0401u, 0160000u,
                                  files[0].name, files[0].ext, 1u,
                                  index_extents, 2u,
                                  (uint64_t)files[0].blocks *
                                      RSX11_BLOCK_SIZE) != 0) {
        goto out;
    }
    if (write_header_slot(&img, &(rsx11_home_t){ .ibsz = 1u, .iblb = iblb },
                          1u, header_block) != 0) {
        goto out;
    }

    for (i = 1; i < 3; i++) {
        if (build_file_header(header_block,
                              files[i].fid.num, files[i].fid.seq,
                              0401u, 0160000u,
                              files[i].name, files[i].ext, 1u,
                              files[i].start_lbn, files[i].blocks,
                              (uint64_t)files[i].blocks *
                                  RSX11_BLOCK_SIZE) != 0) {
            goto out;
        }
        if (write_header_slot(&img, &(rsx11_home_t){ .ibsz = 1u, .iblb = iblb },
                              files[i].fid.num, header_block) != 0) {
            goto out;
        }
    }
    if (build_directory_header(header_block,
                               files[3].fid.num, files[3].fid.seq,
                               0401u, files[3].name, files[3].start_lbn) != 0) {
        goto out;
    }
    if (write_header_slot(&img, &(rsx11_home_t){ .ibsz = 1u, .iblb = iblb },
                          files[3].fid.num, header_block) != 0) {
        goto out;
    }
    if (build_file_header_extents(header_block,
                                  files[4].fid.num, files[4].fid.seq,
                                  0401u, 0160000u,
                                  files[4].name, files[4].ext, 1u,
                                  NULL, 0u, 0u) != 0) {
        goto out;
    }
    if (write_header_slot(&img, &(rsx11_home_t){ .ibsz = 1u, .iblb = iblb },
                          files[4].fid.num, header_block) != 0) {
        goto out;
    }

    memset(mfd_block, 0, sizeof(mfd_block));
    for (i = 0; i < 5u; i++) {
        if (build_dir_record(mfd_block + i * 16u, &files[i].fid,
                             files[i].name, files[i].ext, 1u) != 0) {
            goto out;
        }
    }
    if (write_at(img.fp, (uint64_t)files[3].start_lbn * RSX11_BLOCK_SIZE,
                 mfd_block, sizeof(mfd_block)) != 0) {
        goto out;
    }

    storage_size = (size_t)bitmap_total_blocks * RSX11_BLOCK_SIZE;
    storage_bitmap = malloc(storage_size);
    if (storage_bitmap == NULL) {
        goto out;
    }
    init_storage_bitmap(storage_bitmap, storage_size, unit_blocks);
    for (i = 0; i < 2u; i++) {
        uint32_t b;

        for (b = 0; b < index_extents[i].blocks; b++) {
            storage_bitmap_set_free(storage_bitmap, storage_size,
                                    index_extents[i].lbn + b, 0);
        }
    }
    for (i = 1; i < 4u; i++) {
        uint32_t b;

        for (b = 0; b < files[i].blocks; b++) {
            storage_bitmap_set_free(storage_bitmap, storage_size,
                                    files[i].start_lbn + b, 0);
        }
    }
    update_storage_bitmap_control(storage_bitmap, storage_size, unit_blocks);
    if (write_at(img.fp, (uint64_t)files[1].start_lbn * RSX11_BLOCK_SIZE,
                 storage_bitmap, storage_size) != 0) {
        goto out;
    }

    index_size = RSX11_BLOCK_SIZE;
    index_bitmap = calloc(1u, index_size);
    if (index_bitmap == NULL) {
        goto out;
    }
    for (i = 0; i < 5u; i++) {
        index_bitmap_set(index_bitmap, index_size, files[i].fid.num, 1);
    }
    if (write_at(img.fp, (uint64_t)iblb * RSX11_BLOCK_SIZE,
                 index_bitmap, index_size) != 0) {
        goto out;
    }
    if (fflush(img.fp) != 0) {
        goto out;
    }

    rc = 0;

out:
    free(index_bitmap);
    free(storage_bitmap);
    rsx11_close_image(&img);
    return rc;
}

static void fsck_issue(FILE *out, rsx11_fsck_report_t *report,
                       const rsx11_dirent_t *ent, const char *msg)
{
    char selector[64];

    report->issues++;
    if (ent != NULL) {
        format_entry_selector(ent, selector, sizeof(selector));
        fprintf(out, "issue: %s: %s\n", selector, msg);
    } else {
        fprintf(out, "issue: %s\n", msg);
    }
}

static void fsck_fatal(FILE *out, rsx11_fsck_report_t *report,
                       const rsx11_dirent_t *ent, const char *msg)
{
    report->fatal++;
    fsck_issue(out, report, ent, msg);
}

static int fsck_clear_dir_record(rsx11_image_t *img, const rsx11_dirent_t *ent)
{
    static const uint8_t zero[16];

    if (img == NULL || ent == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (ent->raw_offset + sizeof(zero) > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    return write_at(img->fp, ent->raw_offset, zero, sizeof(zero));
}

static int fsck_rewrite_dir_record(rsx11_image_t *img,
                                   const rsx11_dirent_t *ent,
                                   const rsx11_file_header_t *hdr)
{
    uint8_t rec[16];

    if (img == NULL || ent == NULL || hdr == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (ent->raw_offset + sizeof(rec) > img->size_bytes) {
        errno = EIO;
        return -1;
    }
    if (build_dir_record(rec, &hdr->fid, hdr->name, hdr->ext,
                         hdr->version) != 0) {
        return -1;
    }
    return write_at(img->fp, ent->raw_offset, rec, sizeof(rec));
}

static int fsck_dir_record_matches_header(const rsx11_dirent_t *ent,
                                          const rsx11_file_header_t *hdr)
{
    if (ent == NULL || hdr == NULL) {
        return 0;
    }
    return strcmp(ent->name, hdr->name) == 0 &&
           strcmp(ent->ext, hdr->ext) == 0 &&
           ent->version == hdr->version;
}

static int fsck_scan_directory(rsx11_image_t *img,
                               const rsx11_file_header_t *hdr,
                               const char *dir_name,
                               int repair, FILE *out,
                               rsx11_fsck_report_t *report,
                               int *dir_dirty,
                               rsx11_dirlist_t *entries)
{
    rsx11_fsck_dir_ctx_t ctx;

    ctx.img = img;
    ctx.dir_name = dir_name;
    ctx.repair = repair;
    ctx.out = out;
    ctx.report = report;
    ctx.dir_dirty = dir_dirty;
    ctx.entries = entries;
    return walk_dir_records(img, hdr, fsck_dir_record_cb, &ctx);
}

static int collect_reachable_entries(rsx11_image_t *img,
                                     rsx11_header_cache_t *cache,
                                     int repair, FILE *out,
                                     rsx11_fsck_report_t *report,
                                     int *dir_dirty,
                                     rsx11_dirlist_t *out_entries)
{
    rsx11_fid_t mfd_fid;
    rsx11_file_header_t mfd_hdr;
    rsx11_dirlist_t mfd_entries;
    size_t i;

    memset(out_entries, 0, sizeof(*out_entries));
    memset(&mfd_entries, 0, sizeof(mfd_entries));
    memset(&mfd_fid, 0, sizeof(mfd_fid));
    mfd_fid.num = 4u;
    mfd_fid.seq = 4u;
    if (lookup_header(img, cache, &mfd_fid, &mfd_hdr) != 0) {
        return -1;
    }
    if (fsck_scan_directory(img, &mfd_hdr, "MFD",
                            repair, out, report, dir_dirty,
                            &mfd_entries) != 0) {
        return -1;
    }
    for (i = 0; i < mfd_entries.count; i++) {
        if (append_dirent(out_entries, &mfd_entries.entries[i]) != 0) {
            rsx11_free_dirlist(&mfd_entries);
            return -1;
        }
    }
    for (i = 0; i < mfd_entries.count; i++) {
        rsx11_file_header_t dir_hdr;
        rsx11_dirlist_t ufd_entries;
        char uic[16];
        size_t j;

        if (strcmp(mfd_entries.entries[i].ext, "DIR") != 0 ||
            strcmp(mfd_entries.entries[i].name, "000000") == 0 ||
            !format_uic(mfd_entries.entries[i].name, uic, sizeof(uic))) {
            continue;
        }
        if (lookup_header(img, cache, &mfd_entries.entries[i].fid,
                          &dir_hdr) != 0) {
            continue;
        }
        memset(&ufd_entries, 0, sizeof(ufd_entries));
        if (fsck_scan_directory(img, &dir_hdr, uic,
                                repair, out, report, dir_dirty,
                                &ufd_entries) != 0) {
            continue;
        }
        for (j = 0; j < ufd_entries.count; j++) {
            if (append_dirent(out_entries, &ufd_entries.entries[j]) != 0) {
                rsx11_free_dirlist(&ufd_entries);
                rsx11_free_dirlist(&mfd_entries);
                return -1;
            }
        }
        rsx11_free_dirlist(&ufd_entries);
    }

    rsx11_free_dirlist(&mfd_entries);
    return 0;
}

int rsx11_fsck(rsx11_image_t *img, int repair, FILE *out,
               rsx11_fsck_report_t *report)
{
    rsx11_fsck_report_t local_report;
    rsx11_home_t home;
    rsx11_header_cache_t cache;
    rsx11_dirlist_t entries;
    rsx11_file_header_t bitmap_hdr;
    uint8_t *index_bitmap = NULL;
    uint8_t *storage_bitmap = NULL;
    uint16_t *seen_seq = NULL;
    uint16_t *pred_seq = NULL;
    size_t index_size = 0;
    size_t storage_size = 0;
    uint32_t unit_blocks = 0;
    int dir_dirty = 0;
    int index_dirty = 0;
    int storage_dirty = 0;
    int rc = -1;
    size_t i;

    if (out == NULL) {
        out = stdout;
    }
    if (report == NULL) {
        report = &local_report;
    }
    memset(report, 0, sizeof(*report));
    memset(&cache, 0, sizeof(cache));
    memset(&entries, 0, sizeof(entries));

    if (rsx11_read_home(img, &home) != 0) {
        return -1;
    }
    if (home.vlev != 0401u) {
        fsck_fatal(out, report, NULL, "volume structure is not ODS-1 (0401)");
        errno = EINVAL;
        return -1;
    }
    if (load_allocation_state(img, &cache, &home,
                              &index_bitmap, &index_size,
                              &storage_bitmap, &storage_size,
                              &bitmap_hdr, &unit_blocks) != 0) {
        goto out;
    }
    if (collect_reachable_entries(img, &cache,
                                  repair, out, report, &dir_dirty,
                                  &entries) != 0) {
        goto out;
    }

    seen_seq = calloc((size_t)home.fmax + 1u, sizeof(*seen_seq));
    if (seen_seq == NULL) {
        goto out;
    }
    pred_seq = calloc((size_t)home.fmax + 1u, sizeof(*pred_seq));
    if (pred_seq == NULL) {
        goto out;
    }

    for (i = 1u; i <= home.fmax; i++) {
        rsx11_file_header_t hdr;

        if (!index_bitmap_test(index_bitmap, index_size, (uint16_t)i)) {
            continue;
        }
        if (read_header_slot(img, &home, (uint16_t)i, &hdr) != 0) {
            continue;
        }
        if (!fid_is_null(&hdr.next_fid) && hdr.next_fid.num <= home.fmax) {
            pred_seq[hdr.next_fid.num] = hdr.next_fid.seq;
        }
    }

    for (i = 0; i < entries.count; i++) {
        rsx11_file_header_t slot_hdr;
        rsx11_file_header_t hdr;
        uint32_t fnum;
        uint32_t lbn;
        uint32_t count;
        size_t j;
        size_t missing_blocks = 0u;

        report->checked++;
        fnum = entries.entries[i].fid.num;
        if (fnum == 0u || fnum > home.fmax) {
            fsck_issue(out, report, &entries.entries[i],
                       "directory record references invalid FID");
            if (repair) {
                if (fsck_clear_dir_record(img, &entries.entries[i]) != 0) {
                    fsck_fatal(out, report, &entries.entries[i],
                               "cannot remove directory record with invalid FID");
                } else {
                    dir_dirty = 1;
                    report->repaired++;
                }
            } else {
                report->fatal++;
            }
            continue;
        }
        if (read_header_slot(img, &home, (uint16_t)fnum, &slot_hdr) != 0 ||
            !same_fid(&slot_hdr.fid, &entries.entries[i].fid)) {
            fsck_issue(out, report, &entries.entries[i],
                       "directory record points to missing header");
            if (repair) {
                if (fsck_clear_dir_record(img, &entries.entries[i]) != 0) {
                    fsck_fatal(out, report, &entries.entries[i],
                               "cannot remove directory record with missing header");
                } else {
                    dir_dirty = 1;
                    report->repaired++;
                }
            } else {
                report->fatal++;
            }
            continue;
        }
        if (slot_hdr.segment_number != 0u) {
            fsck_issue(out, report, &entries.entries[i],
                       "directory record points to non-primary header segment");
            if (repair) {
                if (fsck_clear_dir_record(img, &entries.entries[i]) != 0) {
                    fsck_fatal(out, report, &entries.entries[i],
                               "cannot remove directory record with non-primary header");
                } else {
                    dir_dirty = 1;
                    report->repaired++;
                }
            } else {
                report->fatal++;
            }
            continue;
        }
        if (lookup_header(img, &cache, &entries.entries[i].fid, &hdr) != 0) {
            fsck_issue(out, report, &entries.entries[i],
                       "directory record points to inconsistent header chain");
            if (repair) {
                if (fsck_clear_dir_record(img, &entries.entries[i]) != 0) {
                    fsck_fatal(out, report, &entries.entries[i],
                               "cannot remove directory record with inconsistent header chain");
                } else {
                    dir_dirty = 1;
                    report->repaired++;
                }
            } else {
                report->fatal++;
            }
            continue;
        }
        if (!fsck_dir_record_matches_header(&entries.entries[i], &hdr)) {
            fsck_issue(out, report, &entries.entries[i],
                       "directory record name/type/version disagrees with header");
            if (repair) {
                if (fsck_rewrite_dir_record(img, &entries.entries[i], &hdr) != 0) {
                    fsck_fatal(out, report, &entries.entries[i],
                               "cannot rewrite directory record from header");
                    continue;
                }
                dir_dirty = 1;
                report->repaired++;
                strncpy(entries.entries[i].name, hdr.name,
                        sizeof(entries.entries[i].name) - 1u);
                entries.entries[i].name[sizeof(entries.entries[i].name) - 1u] = '\0';
                strncpy(entries.entries[i].ext, hdr.ext,
                        sizeof(entries.entries[i].ext) - 1u);
                entries.entries[i].ext[sizeof(entries.entries[i].ext) - 1u] = '\0';
                entries.entries[i].version = hdr.version;
            } else {
                report->fatal++;
            }
        }
        if (seen_seq[fnum] == entries.entries[i].fid.seq) {
            continue;
        }
        for (j = 0; j < hdr.header_count; j++) {
            if (hdr.header_fids[j].num <= home.fmax) {
                seen_seq[hdr.header_fids[j].num] =
                    hdr.header_fids[j].seq;
            }
        }

        for (j = 0; j < hdr.header_count; j++) {
            if (!index_bitmap_test(index_bitmap, index_size,
                                   hdr.header_fids[j].num)) {
                missing_blocks++;
            }
        }
        if (missing_blocks != 0u) {
            char msg[96];

            snprintf(msg, sizeof(msg),
                     "index bitmap marks %lu header slot(s) free",
                     (unsigned long)missing_blocks);
            fsck_issue(out, report, &entries.entries[i], msg);
            if (repair) {
                for (j = 0; j < hdr.header_count; j++) {
                    index_bitmap_set(index_bitmap, index_size,
                                     hdr.header_fids[j].num, 1);
                }
                index_dirty = 1;
                report->repaired++;
            }
        }

        missing_blocks = 0u;
        for (j = 0; j < hdr.extent_count; j++) {
            uint32_t b;

            lbn = hdr.extents[j].lbn;
            count = hdr.extents[j].blocks;
            if (count == 0u || lbn + count > unit_blocks ||
                (uint64_t)(lbn + count) * RSX11_BLOCK_SIZE > img->size_bytes) {
                fsck_fatal(out, report, &entries.entries[i],
                           "header contains extent outside image bounds");
                count = 0u;
                break;
            }
            for (b = 0; b < count; b++) {
                if (storage_bitmap_is_free(storage_bitmap, storage_size,
                                           lbn + b)) {
                    missing_blocks++;
                }
            }
        }

        if (missing_blocks != 0u) {
            char msg[96];

            snprintf(msg, sizeof(msg),
                     "storage bitmap marks %lu referenced block(s) free",
                     (unsigned long)missing_blocks);
            fsck_issue(out, report, &entries.entries[i], msg);
            if (repair) {
                for (j = 0; j < hdr.extent_count; j++) {
                    uint32_t b;

                    lbn = hdr.extents[j].lbn;
                    count = hdr.extents[j].blocks;
                    if (count == 0u || lbn + count > unit_blocks ||
                        (uint64_t)(lbn + count) * RSX11_BLOCK_SIZE >
                            img->size_bytes) {
                        break;
                    }
                    for (b = 0; b < count; b++) {
                        storage_bitmap_set_free(storage_bitmap, storage_size,
                                                lbn + b, 0);
                    }
                }
                storage_dirty = 1;
                report->repaired++;
            }
        }
    }

    for (i = 1u; i <= home.fmax; i++) {
        rsx11_file_header_t hdr;
        rsx11_file_header_t chain_hdr;
        char selector[64];
        size_t j;

        if (!index_bitmap_test(index_bitmap, index_size, (uint16_t)i)) {
            continue;
        }
        report->checked++;
        if (read_header_slot(img, &home, (uint16_t)i, &hdr) != 0) {
            char msg[96];

            snprintf(msg, sizeof(msg),
                     "index bitmap marks FID %06o allocated, but header slot is empty or invalid",
                     (unsigned)i);
            fsck_issue(out, report, NULL, msg);
            if (repair) {
                index_bitmap_set(index_bitmap, index_size, (uint16_t)i, 0);
                index_dirty = 1;
                report->repaired++;
            } else {
                report->fatal++;
            }
            continue;
        }
        if (seen_seq[i] == hdr.fid.seq) {
            continue;
        }
        if (hdr.segment_number != 0u && pred_seq[i] == hdr.fid.seq) {
            continue;
        }
        if (lookup_header(img, &cache, &hdr.fid, &chain_hdr) != 0) {
            char selector[64];

            format_header_selector(&hdr, selector, sizeof(selector));
            report->issues++;
            fprintf(out, "issue: orphan header %s chain is inconsistent\n",
                    selector);
            if (repair) {
                if (!fid_is_null(&hdr.next_fid) &&
                    hdr.next_fid.num <= home.fmax) {
                    pred_seq[hdr.next_fid.num] = 0u;
                }
                if (release_header_chain_allocation(&hdr,
                                                    index_bitmap, index_size,
                                                    storage_bitmap, storage_size,
                                                    unit_blocks) != 0) {
                    fsck_fatal(out, report, NULL,
                               "cannot free inconsistent orphan header segment");
                    continue;
                }
                index_dirty = 1;
                storage_dirty = 1;
                report->repaired++;
            } else {
                report->fatal++;
            }
            continue;
        }
        for (j = 0; j < chain_hdr.header_count; j++) {
            if (chain_hdr.header_fids[j].num <= home.fmax) {
                seen_seq[chain_hdr.header_fids[j].num] =
                    chain_hdr.header_fids[j].seq;
            }
        }

        format_header_selector(&chain_hdr, selector, sizeof(selector));
        report->issues++;
        fprintf(out,
                "issue: orphan header %s is allocated but not referenced by any directory\n",
                selector);
        if (repair) {
            if (release_header_chain_allocation(&chain_hdr,
                                                index_bitmap, index_size,
                                                storage_bitmap, storage_size,
                                                unit_blocks) != 0) {
                fsck_fatal(out, report, NULL,
                           "cannot free orphan header chain: extent outside image bounds");
                continue;
            }
            index_dirty = 1;
            storage_dirty = 1;
            report->repaired++;
        }
    }

    if (repair) {
        if (index_dirty &&
            write_at(img->fp, (uint64_t)home.iblb * RSX11_BLOCK_SIZE,
                     index_bitmap, index_size) != 0) {
            goto out;
        }
        if (storage_dirty &&
            write_header_data(img, &bitmap_hdr,
                              storage_bitmap, storage_size) != 0) {
            goto out;
        }
        if ((dir_dirty || index_dirty || storage_dirty) &&
            fflush(img->fp) != 0) {
            goto out;
        }
    }

    fprintf(out,
            "summary: checked %lu record(s), %lu issue(s), %lu repaired, %lu fatal\n",
            (unsigned long)report->checked,
            (unsigned long)report->issues,
            (unsigned long)report->repaired,
            (unsigned long)report->fatal);
    if (repair && report->fatal != 0u && report->repaired != 0u) {
        fprintf(out, "repair: soft bitmap issues fixed, fatal issues remain\n");
    } else if (repair && report->fatal != 0u) {
        fprintf(out, "repair: nothing fixed, fatal issues remain\n");
    }
    if (report->issues == 0u) {
        fprintf(out, "fsck: clean\n");
    } else if (repair && report->fatal == 0u &&
               report->issues == report->repaired) {
        fprintf(out, "fsck: repaired\n");
    }

    rc = 0;

out:
    free(pred_seq);
    free(seen_seq);
    free(storage_bitmap);
    free(index_bitmap);
    rsx11_free_dirlist(&entries);
    free_header_cache(&cache);
    return rc;
}

void rsx11_free_dirlist(rsx11_dirlist_t *list)
{
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
}
