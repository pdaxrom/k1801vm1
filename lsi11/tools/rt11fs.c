#include "rt11fs.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define RT11_DIR_START_BLOCK 000006
#define RT11_DIR_SEGMENT_BLOCKS 2u

#define RT11_PARTITION_BLOCKS 65536u
#define RT11_PARTITION_USABLE 65535u
#define RT11_PARTITION_MAX 256u

#define RT11_SEGMENTS_MAX 31u
#define RT11_SEGMENTS_SMALL 16u
#define RT11_SEGMENTS_THRESHOLD 10000u

#define RT11_HOME_CLUSTER_OFF 0722
#define RT11_HOME_DIR_START_OFF 0724
#define RT11_HOME_SYSVER_OFF 0726
#define RT11_HOME_VOLID_OFF 0730
#define RT11_HOME_OWNER_OFF 0744
#define RT11_HOME_SYSID_OFF 0760
#define RT11_HOME_CKSUM_OFF 0776
#define RT11_HOME_FIELD_LEN 12u

typedef struct {
    uint16_t total_segments;
    uint16_t next_segment;
    uint16_t highest_segment;
    uint16_t extra_bytes;
    uint16_t data_start_block;
} rt11_dirseg_hdr_t;

static const char rt11_rad50_chars[40] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";

static uint16_t rt11_date_now(void);
static int rt11_read_block(rt11_image_t *img, uint32_t block, uint8_t *buf);
static int rt11_write_block(rt11_image_t *img, uint32_t block,
                            const uint8_t *buf);
static int rt11_squeeze_segment(rt11_image_t *img, uint16_t dir_start,
                                uint16_t seg_num, uint16_t *next_out);

static uint16_t rt11_get_word(const uint8_t *buf, size_t word_index)
{
    size_t off = word_index * 2;
    return (uint16_t)(buf[off] | ((uint16_t)buf[off + 1] << 8));
}

static int rt11_move_blocks(rt11_image_t *img, uint32_t from, uint32_t to,
                            uint32_t count)
{
    uint32_t i;

    if (!img || !img->fp) {
        errno = EINVAL;
        return -1;
    }
    if (count == 0 || from == to) {
        return 0;
    }
    if (from + count > img->volume_blocks || to + count > img->volume_blocks) {
        errno = EINVAL;
        return -1;
    }

    if (to < from) {
        for (i = 0; i < count; i++) {
            uint8_t buf[RT11_BLOCK_SIZE];
            if (rt11_read_block(img, from + i, buf) != 0) {
                return -1;
            }
            if (rt11_write_block(img, to + i, buf) != 0) {
                return -1;
            }
        }
    } else {
        for (i = count; i > 0; i--) {
            uint8_t buf[RT11_BLOCK_SIZE];
            uint32_t idx = i - 1u;
            if (rt11_read_block(img, from + idx, buf) != 0) {
                return -1;
            }
            if (rt11_write_block(img, to + idx, buf) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static void rt11_set_word(uint8_t *buf, size_t word_index, uint16_t val)
{
    size_t off = word_index * 2;
    buf[off] = (uint8_t)(val & 0xffu);
    buf[off + 1] = (uint8_t)((val >> 8) & 0xffu);
}

static int rt11_seek(rt11_image_t *img, uint32_t block, uint32_t extra_bytes)
{
    uint32_t abs_block;
    off_t off;

    if (!img || !img->fp) {
        errno = EINVAL;
        return -1;
    }
    if (block >= img->volume_blocks) {
        errno = EINVAL;
        return -1;
    }

    abs_block = img->base_block + block;
    if (abs_block >= img->total_blocks) {
        errno = EINVAL;
        return -1;
    }

    off = (off_t)abs_block * (off_t)RT11_BLOCK_SIZE + (off_t)extra_bytes;
    if (fseeko(img->fp, off, SEEK_SET) != 0) {
        return -1;
    }
    return 0;
}

static int rt11_read_block(rt11_image_t *img, uint32_t block, uint8_t *buf)
{
    if (rt11_seek(img, block, 0) != 0) {
        return -1;
    }
    if (fread(buf, 1, RT11_BLOCK_SIZE, img->fp) != RT11_BLOCK_SIZE) {
        return -1;
    }
    return 0;
}

static int rt11_write_block(rt11_image_t *img, uint32_t block,
                            const uint8_t *buf)
{
    if (rt11_seek(img, block, 0) != 0) {
        return -1;
    }
    if (fwrite(buf, 1, RT11_BLOCK_SIZE, img->fp) != RT11_BLOCK_SIZE) {
        return -1;
    }
    return 0;
}

static int rt11_read_segment(rt11_image_t *img, uint32_t start_block,
                             uint8_t *buf)
{
    if (start_block + RT11_DIR_SEGMENT_BLOCKS > img->volume_blocks) {
        errno = EINVAL;
        return -1;
    }
    if (rt11_seek(img, start_block, 0) != 0) {
        return -1;
    }
    if (fread(buf, 1, RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS, img->fp) !=
        RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS) {
        return -1;
    }
    return 0;
}

static int rt11_write_segment(rt11_image_t *img, uint32_t start_block,
                              const uint8_t *buf)
{
    if (start_block + RT11_DIR_SEGMENT_BLOCKS > img->volume_blocks) {
        errno = EINVAL;
        return -1;
    }
    if (rt11_seek(img, start_block, 0) != 0) {
        return -1;
    }
    if (fwrite(buf, 1, RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS, img->fp) !=
        RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS) {
        return -1;
    }
    return 0;
}

static int rt11_rad50_val(int ch)
{
    if (ch == ' ') {
        return 0;
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 1;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 30;
    }
    if (ch == '$') {
        return 27;
    }
    if (ch == '.') {
        return 28;
    }
    if (ch == '?') {
        return 29;
    }
    return -1;
}

static uint16_t rt11_rad50_encode3(const char *s)
{
    int c0 = rt11_rad50_val((unsigned char)s[0]);
    int c1 = rt11_rad50_val((unsigned char)s[1]);
    int c2 = rt11_rad50_val((unsigned char)s[2]);

    if (c0 < 0) {
        c0 = 0;
    }
    if (c1 < 0) {
        c1 = 0;
    }
    if (c2 < 0) {
        c2 = 0;
    }

    return (uint16_t)(c0 * 1600 + c1 * 40 + c2);
}

static void rt11_rad50_decode3(uint16_t w, char out[3])
{
    unsigned int c0 = (unsigned int)(w / 1600);
    unsigned int rem = (unsigned int)(w % 1600);
    unsigned int c1 = rem / 40;
    unsigned int c2 = rem % 40;

    out[0] = rt11_rad50_chars[c0 < 40 ? c0 : 0];
    out[1] = rt11_rad50_chars[c1 < 40 ? c1 : 0];
    out[2] = rt11_rad50_chars[c2 < 40 ? c2 : 0];
}

static void rt11_trim_spaces(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') {
        s[len - 1] = '\0';
        len--;
    }
}

static int rt11_parse_segment_header(const uint8_t *buf, rt11_dirseg_hdr_t *hdr)
{
    if (!hdr) {
        return -1;
    }
    hdr->total_segments = rt11_get_word(buf, 0);
    hdr->next_segment = rt11_get_word(buf, 1);
    hdr->highest_segment = rt11_get_word(buf, 2);
    hdr->extra_bytes = rt11_get_word(buf, 3);
    hdr->data_start_block = rt11_get_word(buf, 4);
    return 0;
}

static int rt11_entry_layout(const rt11_dirseg_hdr_t *hdr,
                             uint16_t *entry_words_out,
                             uint16_t *max_entries_out)
{
    uint16_t entry_words;
    uint16_t max_entries;

    if (!hdr || !entry_words_out || !max_entries_out) {
        errno = EINVAL;
        return -1;
    }
    if ((hdr->extra_bytes & 1u) != 0) {
        errno = EINVAL;
        return -1;
    }

    entry_words = (uint16_t)(7u + hdr->extra_bytes / 2u);
    if (entry_words == 0) {
        errno = EINVAL;
        return -1;
    }

    max_entries = (uint16_t)((512u - 5u) / entry_words);
    if (max_entries == 0) {
        errno = EINVAL;
        return -1;
    }

    *entry_words_out = entry_words;
    *max_entries_out = max_entries;
    return 0;
}

static int rt11_entry_is_eos(uint16_t status)
{
    return (status & RT11_E_EOS) != 0;
}

static int rt11_entry_is_empty(uint16_t status)
{
    return (status & RT11_E_MPTY) != 0;
}

static int rt11_entry_is_file(uint16_t status)
{
    return (status & (RT11_E_PERM | RT11_E_TENT)) != 0;
}

static uint16_t rt11_default_segments(uint32_t total_blocks)
{
    if (total_blocks <= RT11_SEGMENTS_THRESHOLD) {
        return RT11_SEGMENTS_SMALL;
    }
    return RT11_SEGMENTS_MAX;
}

int rt11_open_image(rt11_image_t *img, const char *path, const char *mode)
{
    off_t size;

    if (!img || !path || !mode) {
        return -1;
    }

    memset(img, 0, sizeof(*img));
    img->fp = fopen(path, mode);
    if (!img->fp) {
        return -1;
    }

    if (fseeko(img->fp, 0, SEEK_END) != 0) {
        fclose(img->fp);
        img->fp = NULL;
        return -1;
    }

    size = ftello(img->fp);
    if (size < 0 || (size % RT11_BLOCK_SIZE) != 0) {
        fclose(img->fp);
        img->fp = NULL;
        errno = EINVAL;
        return -1;
    }

    img->total_blocks = (uint32_t)(size / RT11_BLOCK_SIZE);
    if (fseeko(img->fp, 0, SEEK_SET) != 0) {
        fclose(img->fp);
        img->fp = NULL;
        return -1;
    }

    if (rt11_set_partition(img, 0) != 0) {
        fclose(img->fp);
        img->fp = NULL;
        return -1;
    }

    return 0;
}

void rt11_close_image(rt11_image_t *img)
{
    if (!img) {
        return;
    }
    if (img->fp) {
        fclose(img->fp);
        img->fp = NULL;
    }
    img->total_blocks = 0;
    img->base_block = 0;
    img->partition_blocks = 0;
    img->volume_blocks = 0;
    img->partition_number = 0;
}

int rt11_set_partition(rt11_image_t *img, uint32_t partition)
{
    uint32_t base;
    uint32_t part_blocks;

    if (!img || !img->fp) {
        errno = EINVAL;
        return -1;
    }
    if (partition >= RT11_PARTITION_MAX) {
        errno = EINVAL;
        return -1;
    }

    base = partition * RT11_PARTITION_BLOCKS;
    if (base >= img->total_blocks) {
        errno = EINVAL;
        return -1;
    }

    part_blocks = img->total_blocks - base;
    if (part_blocks > RT11_PARTITION_BLOCKS) {
        part_blocks = RT11_PARTITION_BLOCKS;
    }

    img->base_block = base;
    img->partition_blocks = part_blocks;
    img->volume_blocks = part_blocks;
    if (img->volume_blocks >= RT11_PARTITION_BLOCKS) {
        img->volume_blocks = RT11_PARTITION_USABLE;
    }
    img->partition_number = partition;
    return 0;
}

int rt11_read_home(rt11_image_t *img, uint16_t *dir_start_out)
{
    uint8_t buf[RT11_BLOCK_SIZE];
    uint16_t dir_start;

    if (!img || !img->fp || !dir_start_out) {
        return -1;
    }

    if (rt11_read_block(img, 1, buf) != 0) {
        return -1;
    }

    dir_start =
        rt11_get_word(buf, (size_t)RT11_HOME_DIR_START_OFF / 2u);
    if (dir_start == 0) {
        dir_start = RT11_DIR_START_BLOCK;
    }

    *dir_start_out = dir_start;
    return 0;
}

int rt11_name_from_host(const char *host, rt11_name_t *out)
{
    char name_buf[7];
    char ext_buf[4];
    const char *dot;
    size_t name_len;
    size_t ext_len;
    size_t i;

    if (!host || !out) {
        return -1;
    }

    dot = strchr(host, '.');
    if (!dot) {
        name_len = strlen(host);
        ext_len = 0;
    } else {
        name_len = (size_t)(dot - host);
        ext_len = strlen(dot + 1);
    }

    if (name_len == 0 || name_len > 6 || ext_len > 3) {
        return -1;
    }

    memset(name_buf, ' ', sizeof(name_buf));
    memset(ext_buf, ' ', sizeof(ext_buf));

    for (i = 0; i < name_len; i++) {
        int ch = toupper((unsigned char)host[i]);
        if (rt11_rad50_val(ch) < 0 || ch == ' ') {
            return -1;
        }
        name_buf[i] = (char)ch;
    }

    if (dot) {
        for (i = 0; i < ext_len; i++) {
            int ch = toupper((unsigned char)dot[1 + i]);
            if (rt11_rad50_val(ch) < 0 || ch == ' ') {
                return -1;
            }
            ext_buf[i] = (char)ch;
        }
    }

    out->name_words[0] = rt11_rad50_encode3(&name_buf[0]);
    out->name_words[1] = rt11_rad50_encode3(&name_buf[3]);
    out->ext_word = rt11_rad50_encode3(&ext_buf[0]);

    return 0;
}

void rt11_name_to_host(const rt11_name_t *name, char *out, size_t out_size,
                        int lower)
{
    char name_chars[7];
    char ext_chars[4];
    char word[3];
    size_t i;

    if (!name || !out || out_size == 0) {
        return;
    }

    rt11_rad50_decode3(name->name_words[0], word);
    memcpy(&name_chars[0], word, 3);
    rt11_rad50_decode3(name->name_words[1], word);
    memcpy(&name_chars[3], word, 3);
    name_chars[6] = '\0';

    rt11_rad50_decode3(name->ext_word, word);
    memcpy(&ext_chars[0], word, 3);
    ext_chars[3] = '\0';

    rt11_trim_spaces(name_chars);
    rt11_trim_spaces(ext_chars);

    if (lower) {
        for (i = 0; name_chars[i]; i++) {
            name_chars[i] = (char)tolower((unsigned char)name_chars[i]);
        }
        for (i = 0; ext_chars[i]; i++) {
            ext_chars[i] = (char)tolower((unsigned char)ext_chars[i]);
        }
    }

    if (ext_chars[0]) {
        snprintf(out, out_size, "%s.%s", name_chars, ext_chars);
    } else {
        snprintf(out, out_size, "%s", name_chars);
    }
}

static int rt11_read_dir_entries(rt11_image_t *img, uint16_t dir_start,
                                 rt11_dirlist_t *out)
{
    rt11_dirseg_hdr_t hdr;
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    uint32_t seg_block;
    uint16_t seg_num;
    uint16_t total_segments = 0;
    uint16_t next_seg = 1;
    uint16_t entry_words;
    uint16_t max_entries;
    size_t cap = 0;
    size_t count = 0;
    uint32_t guard = 0;

    if (!img || !out) {
        return -1;
    }

    out->entries = NULL;
    out->count = 0;

    while (next_seg != 0) {
        seg_num = next_seg;
        seg_block = (uint32_t)dir_start +
                    (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;
        if (rt11_read_segment(img, seg_block, segbuf) != 0) {
            return -1;
        }
        if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
            return -1;
        }
        if (rt11_entry_layout(&hdr, &entry_words, &max_entries) != 0) {
            return -1;
        }

        if (total_segments == 0) {
            total_segments = hdr.total_segments;
            if (total_segments == 0) {
                total_segments = 1;
            }
        }

        {
            uint32_t cur_block = hdr.data_start_block;
            uint16_t i;
            for (i = 0; i < max_entries; i++) {
                size_t base_word = 5u + (size_t)i * entry_words;
                uint16_t status = rt11_get_word(segbuf, base_word);
                uint16_t length = rt11_get_word(segbuf, base_word + 4u);
                if (rt11_entry_is_eos(status)) {
                    break;
                }

                if (rt11_entry_is_file(status)) {
                    rt11_dirent_t ent;
                    rt11_name_t name;
                    char name_buf[16];

                    name.name_words[0] = rt11_get_word(segbuf, base_word + 1u);
                    name.name_words[1] = rt11_get_word(segbuf, base_word + 2u);
                    name.ext_word = rt11_get_word(segbuf, base_word + 3u);

                    rt11_name_to_host(&name, name_buf, sizeof(name_buf), 0);

                    memset(&ent, 0, sizeof(ent));
                    ent.status = status;
                    ent.length = length;
                    ent.job = rt11_get_word(segbuf, base_word + 5u);
                    ent.date = rt11_get_word(segbuf, base_word + 6u);
                    ent.start_block = cur_block;
                    ent.raw_offset =
                        (uint32_t)seg_block * RT11_BLOCK_SIZE +
                        (uint32_t)base_word * 2u;
                    ent.segment = seg_num;
                    ent.entry_index = i;

                    {
                        char *dot = strchr(name_buf, '.');
                        if (dot) {
                            size_t nlen = (size_t)(dot - name_buf);
                            if (nlen >= sizeof(ent.name)) {
                                nlen = sizeof(ent.name) - 1;
                            }
                            memcpy(ent.name, name_buf, nlen);
                            ent.name[nlen] = '\0';
                            strncpy(ent.ext, dot + 1, sizeof(ent.ext) - 1);
                            ent.ext[sizeof(ent.ext) - 1] = '\0';
                        } else {
                            strncpy(ent.name, name_buf, sizeof(ent.name) - 1);
                            ent.name[sizeof(ent.name) - 1] = '\0';
                            ent.ext[0] = '\0';
                        }
                    }

                    if (count == cap) {
                        size_t new_cap = cap ? cap * 2 : 16;
                        rt11_dirent_t *new_entries =
                            (rt11_dirent_t *)realloc(out->entries,
                                                     new_cap * sizeof(*out->entries));
                        if (!new_entries) {
                            rt11_free_dirlist(out);
                            return -1;
                        }
                        out->entries = new_entries;
                        cap = new_cap;
                    }
                    out->entries[count++] = ent;
                }

                cur_block += length;
            }
        }

        out->count = count;
        next_seg = hdr.next_segment;
        guard++;
        if (guard > total_segments + 4) {
            break;
        }
    }

    out->count = count;
    return 0;
}

int rt11_read_directory(rt11_image_t *img, rt11_dirlist_t *out)
{
    uint16_t dir_start;

    if (rt11_read_home(img, &dir_start) != 0) {
        return -1;
    }
    return rt11_read_dir_entries(img, dir_start, out);
}

void rt11_free_dirlist(rt11_dirlist_t *list)
{
    if (!list) {
        return;
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
}

int rt11_find_file(rt11_image_t *img, const rt11_name_t *name,
                   rt11_dirent_t *ent_out)
{
    rt11_dirseg_hdr_t hdr;
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    uint16_t dir_start;
    uint16_t next_seg = 1;
    uint16_t total_segments = 0;
    uint32_t guard = 0;

    if (!img || !name || !ent_out) {
        return -1;
    }

    if (rt11_read_home(img, &dir_start) != 0) {
        return -1;
    }

    while (next_seg != 0) {
        uint16_t seg_num = next_seg;
        uint32_t seg_block = (uint32_t)dir_start +
                             (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;
        uint16_t entry_words;
        uint16_t max_entries;

        if (rt11_read_segment(img, seg_block, segbuf) != 0) {
            return -1;
        }
        if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
            return -1;
        }
        if (rt11_entry_layout(&hdr, &entry_words, &max_entries) != 0) {
            return -1;
        }

        if (total_segments == 0) {
            total_segments = hdr.total_segments;
            if (total_segments == 0) {
                total_segments = 1;
            }
        }

        {
            uint32_t cur_block = hdr.data_start_block;
            uint16_t i;
            for (i = 0; i < max_entries; i++) {
                size_t base_word = 5u + (size_t)i * entry_words;
                uint16_t status = rt11_get_word(segbuf, base_word);
                uint16_t length = rt11_get_word(segbuf, base_word + 4u);
                if (rt11_entry_is_eos(status)) {
                    break;
                }
                if (rt11_entry_is_file(status)) {
                    uint16_t n0 = rt11_get_word(segbuf, base_word + 1u);
                    uint16_t n1 = rt11_get_word(segbuf, base_word + 2u);
                    uint16_t e0 = rt11_get_word(segbuf, base_word + 3u);
                    if (n0 == name->name_words[0] &&
                        n1 == name->name_words[1] && e0 == name->ext_word) {
                        rt11_dirent_t ent;
                        rt11_name_t nm;
                        char name_buf[16];

                        nm.name_words[0] = n0;
                        nm.name_words[1] = n1;
                        nm.ext_word = e0;
                        rt11_name_to_host(&nm, name_buf, sizeof(name_buf), 0);

                        memset(&ent, 0, sizeof(ent));
                        ent.status = status;
                        ent.length = length;
                        ent.job = rt11_get_word(segbuf, base_word + 5u);
                        ent.date = rt11_get_word(segbuf, base_word + 6u);
                        ent.start_block = cur_block;
                        ent.raw_offset =
                            (uint32_t)seg_block * RT11_BLOCK_SIZE +
                            (uint32_t)base_word * 2u;
                        ent.segment = seg_num;
                        ent.entry_index = i;

                        {
                            char *dot = strchr(name_buf, '.');
                            if (dot) {
                                size_t nlen = (size_t)(dot - name_buf);
                                if (nlen >= sizeof(ent.name)) {
                                    nlen = sizeof(ent.name) - 1;
                                }
                                memcpy(ent.name, name_buf, nlen);
                                ent.name[nlen] = '\0';
                                strncpy(ent.ext, dot + 1, sizeof(ent.ext) - 1);
                                ent.ext[sizeof(ent.ext) - 1] = '\0';
                            } else {
                                strncpy(ent.name, name_buf,
                                        sizeof(ent.name) - 1);
                                ent.name[sizeof(ent.name) - 1] = '\0';
                                ent.ext[0] = '\0';
                            }
                        }

                        *ent_out = ent;
                        return 0;
                    }
                }
                cur_block += length;
            }
        }

        next_seg = hdr.next_segment;
        guard++;
        if (guard > total_segments + 4) {
            break;
        }
    }

    errno = ENOENT;
    return -1;
}

int rt11_extract_file(rt11_image_t *img, const rt11_dirent_t *ent,
                      const char *out_path)
{
    uint8_t buf[RT11_BLOCK_SIZE];
    uint32_t i;
    FILE *out;

    if (!img || !ent || !out_path) {
        return -1;
    }

    if (ent->start_block + ent->length > img->volume_blocks) {
        errno = EINVAL;
        return -1;
    }

    out = fopen(out_path, "wb");
    if (!out) {
        return -1;
    }

    for (i = 0; i < ent->length; i++) {
        if (rt11_read_block(img, ent->start_block + i, buf) != 0) {
            fclose(out);
            return -1;
        }
        if (fwrite(buf, 1, RT11_BLOCK_SIZE, out) != RT11_BLOCK_SIZE) {
            fclose(out);
            return -1;
        }
    }

    fclose(out);
    return 0;
}

static int rt11_get_file_size(const char *path, uint64_t *size_out)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    *size_out = (uint64_t)st.st_size;
    return 0;
}

static void rt11_write_entry(uint8_t *segbuf, size_t base_word,
                             uint16_t entry_words, uint16_t status,
                             const rt11_name_t *name, uint16_t length,
                             uint16_t job, uint16_t date)
{
    size_t i;

    rt11_set_word(segbuf, base_word, status);
    rt11_set_word(segbuf, base_word + 1u, name ? name->name_words[0] : 0);
    rt11_set_word(segbuf, base_word + 2u, name ? name->name_words[1] : 0);
    rt11_set_word(segbuf, base_word + 3u, name ? name->ext_word : 0);
    rt11_set_word(segbuf, base_word + 4u, length);
    rt11_set_word(segbuf, base_word + 5u, job);
    rt11_set_word(segbuf, base_word + 6u, date);

    for (i = 7; i < entry_words; i++) {
        rt11_set_word(segbuf, base_word + i, 0);
    }
}

static int rt11_insert_entry(uint8_t *segbuf, uint16_t entry_words,
                             uint16_t max_entries, uint16_t insert_index,
                             uint16_t eos_index)
{
    size_t entry_bytes = (size_t)entry_words * 2u;
    size_t base = 5u * 2u;
    size_t src;
    size_t bytes;

    if (eos_index >= max_entries - 1u) {
        return -1;
    }
    if (insert_index > eos_index + 1u) {
        return -1;
    }

    src = base + (size_t)insert_index * entry_bytes;
    bytes = (size_t)(eos_index - insert_index + 1u) * entry_bytes;
    memmove(segbuf + src + entry_bytes, segbuf + src, bytes);
    memset(segbuf + src, 0, entry_bytes);
    return 0;
}

int rt11_add_file(rt11_image_t *img, const char *host_path,
                  const rt11_name_t *name)
{
    rt11_dirseg_hdr_t hdr;
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    uint16_t dir_start;
    uint16_t next_seg = 1;
    uint16_t total_segments = 0;
    uint32_t guard = 0;
    uint64_t file_size;
    uint32_t needed_blocks;
    FILE *in = NULL;
    uint16_t date = 0;

    if (!img || !host_path || !name) {
        return -1;
    }

    if (rt11_get_file_size(host_path, &file_size) != 0) {
        return -1;
    }
    needed_blocks =
        (uint32_t)((file_size + RT11_BLOCK_SIZE - 1u) / RT11_BLOCK_SIZE);

    if (needed_blocks == 0) {
        needed_blocks = 1;
    }
    date = rt11_date_now();

    {
        rt11_dirent_t existing;
        if (rt11_find_file(img, name, &existing) == 0) {
            errno = EEXIST;
            return -1;
        }
    }

    if (rt11_read_home(img, &dir_start) != 0) {
        return -1;
    }

    while (next_seg != 0) {
        uint16_t seg_num = next_seg;
        uint32_t seg_block = (uint32_t)dir_start +
                             (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;
        uint16_t entry_words;
        uint16_t max_entries;
        uint16_t eos_index = 0xffffu;
        uint16_t i;
        uint32_t cur_block;
        uint16_t candidate = 0xffffu;
        uint16_t candidate_status = 0;
        uint16_t candidate_len = 0;
        uint32_t candidate_start = 0;

        if (rt11_read_segment(img, seg_block, segbuf) != 0) {
            return -1;
        }
        if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
            return -1;
        }
        if (rt11_entry_layout(&hdr, &entry_words, &max_entries) != 0) {
            return -1;
        }

        if (total_segments == 0) {
            total_segments = hdr.total_segments;
            if (total_segments == 0) {
                total_segments = 1;
            }
        }

        cur_block = hdr.data_start_block;
        for (i = 0; i < max_entries; i++) {
            size_t base_word = 5u + (size_t)i * entry_words;
            uint16_t status = rt11_get_word(segbuf, base_word);
            uint16_t length = rt11_get_word(segbuf, base_word + 4u);

            if (rt11_entry_is_eos(status)) {
                eos_index = i;
                if (candidate == 0xffffu && length >= needed_blocks) {
                    candidate = i;
                    candidate_status = status;
                    candidate_len = length;
                    candidate_start = cur_block;
                }
                break;
            }

            if (rt11_entry_is_empty(status) && candidate == 0xffffu &&
                length >= needed_blocks) {
                candidate = i;
                candidate_status = status;
                candidate_len = length;
                candidate_start = cur_block;
            }

            cur_block += length;
        }

        if (candidate != 0xffffu) {
            size_t base_word = 5u + (size_t)candidate * entry_words;
            uint32_t data_off = candidate_start;
            uint16_t remaining = (uint16_t)(candidate_len - needed_blocks);

            if (data_off + needed_blocks > img->volume_blocks) {
                errno = ENOSPC;
                return -1;
            }

            in = fopen(host_path, "rb");
            if (!in) {
                return -1;
            }

            if (rt11_seek(img, data_off, 0) != 0) {
                fclose(in);
                return -1;
            }

            for (i = 0; i < needed_blocks; i++) {
                uint8_t buf[RT11_BLOCK_SIZE];
                size_t got = fread(buf, 1, RT11_BLOCK_SIZE, in);
                if (got < RT11_BLOCK_SIZE) {
                    memset(buf + got, 0, RT11_BLOCK_SIZE - got);
                }
                if (fwrite(buf, 1, RT11_BLOCK_SIZE, img->fp) !=
                    RT11_BLOCK_SIZE) {
                    fclose(in);
                    return -1;
                }
            }

            fclose(in);
            in = NULL;
            if (fflush(img->fp) != 0) {
                return -1;
            }

            if (rt11_entry_is_eos(candidate_status)) {
                if (rt11_insert_entry(segbuf, entry_words, max_entries,
                                      candidate + 1u, eos_index) != 0) {
                    errno = ENOSPC;
                    return -1;
                }
                rt11_write_entry(segbuf, base_word, entry_words, RT11_E_PERM,
                                 name, (uint16_t)needed_blocks, 0, date);
                rt11_write_entry(segbuf,
                                 5u + (size_t)(candidate + 1u) * entry_words,
                                 entry_words, RT11_E_EOS, NULL, remaining, 0,
                                 0);
            } else {
                if (remaining > 0) {
                    if (rt11_insert_entry(segbuf, entry_words, max_entries,
                                          candidate + 1u, eos_index) != 0) {
                        errno = ENOSPC;
                        return -1;
                    }
                    rt11_write_entry(
                        segbuf, 5u + (size_t)(candidate + 1u) * entry_words,
                        entry_words, RT11_E_MPTY, NULL, remaining, 0, 0);
                }
                rt11_write_entry(segbuf, base_word, entry_words, RT11_E_PERM,
                                 name, (uint16_t)needed_blocks, 0, date);
            }

            if (rt11_write_segment(img, seg_block, segbuf) != 0) {
                return -1;
            }

            return 0;
        }

        next_seg = hdr.next_segment;
        guard++;
        if (guard > total_segments + 4) {
            break;
        }
    }

    errno = ENOSPC;
    return -1;
}

int rt11_remove_file(rt11_image_t *img, const rt11_name_t *name, int force)
{
    rt11_dirseg_hdr_t hdr;
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    uint16_t dir_start;
    uint16_t next_seg = 1;
    uint16_t total_segments = 0;
    uint32_t guard = 0;

    if (!img || !name) {
        return -1;
    }

    if (rt11_read_home(img, &dir_start) != 0) {
        return -1;
    }

    while (next_seg != 0) {
        uint16_t seg_num = next_seg;
        uint32_t seg_block = (uint32_t)dir_start +
                             (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;
        uint16_t entry_words;
        uint16_t max_entries;
        uint16_t i;

        if (rt11_read_segment(img, seg_block, segbuf) != 0) {
            return -1;
        }
        if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
            return -1;
        }
        if (rt11_entry_layout(&hdr, &entry_words, &max_entries) != 0) {
            return -1;
        }

        if (total_segments == 0) {
            total_segments = hdr.total_segments;
            if (total_segments == 0) {
                total_segments = 1;
            }
        }

        for (i = 0; i < max_entries; i++) {
            size_t base_word = 5u + (size_t)i * entry_words;
            uint16_t status = rt11_get_word(segbuf, base_word);
            if (rt11_entry_is_eos(status)) {
                break;
            }
            if (rt11_entry_is_file(status)) {
                uint16_t n0 = rt11_get_word(segbuf, base_word + 1u);
                uint16_t n1 = rt11_get_word(segbuf, base_word + 2u);
                uint16_t e0 = rt11_get_word(segbuf, base_word + 3u);
                if (n0 == name->name_words[0] &&
                    n1 == name->name_words[1] && e0 == name->ext_word) {
                    uint16_t length = rt11_get_word(segbuf, base_word + 4u);
                    if ((status & RT11_E_PROT) && !force) {
                        errno = EACCES;
                        return -1;
                    }
                    rt11_write_entry(segbuf, base_word, entry_words,
                                     RT11_E_MPTY, NULL, length, 0, 0);
                    if (rt11_write_segment(img, seg_block, segbuf) != 0) {
                        return -1;
                    }
                    return 0;
                }
            }
        }

        next_seg = hdr.next_segment;
        guard++;
        if (guard > total_segments + 4) {
            break;
        }
    }

    errno = ENOENT;
    return -1;
}

int rt11_set_protect(rt11_image_t *img, const rt11_name_t *name, int protect)
{
    rt11_dirseg_hdr_t hdr;
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    uint16_t dir_start;
    uint16_t next_seg = 1;
    uint16_t total_segments = 0;
    uint32_t guard = 0;

    if (!img || !name) {
        return -1;
    }

    if (rt11_read_home(img, &dir_start) != 0) {
        return -1;
    }

    while (next_seg != 0) {
        uint16_t seg_num = next_seg;
        uint32_t seg_block = (uint32_t)dir_start +
                             (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;
        uint16_t entry_words;
        uint16_t max_entries;
        uint16_t i;

        if (rt11_read_segment(img, seg_block, segbuf) != 0) {
            return -1;
        }
        if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
            return -1;
        }
        if (rt11_entry_layout(&hdr, &entry_words, &max_entries) != 0) {
            return -1;
        }

        if (total_segments == 0) {
            total_segments = hdr.total_segments;
            if (total_segments == 0) {
                total_segments = 1;
            }
        }

        for (i = 0; i < max_entries; i++) {
            size_t base_word = 5u + (size_t)i * entry_words;
            uint16_t status = rt11_get_word(segbuf, base_word);
            if (rt11_entry_is_eos(status)) {
                break;
            }
            if (rt11_entry_is_file(status)) {
                uint16_t n0 = rt11_get_word(segbuf, base_word + 1u);
                uint16_t n1 = rt11_get_word(segbuf, base_word + 2u);
                uint16_t e0 = rt11_get_word(segbuf, base_word + 3u);
                if (n0 == name->name_words[0] &&
                    n1 == name->name_words[1] && e0 == name->ext_word) {
                    if (protect && !(status & RT11_E_PERM)) {
                        errno = EINVAL;
                        return -1;
                    }
                    if (protect) {
                        status |= RT11_E_PROT;
                    } else {
                        status &= (uint16_t)~RT11_E_PROT;
                    }
                    rt11_set_word(segbuf, base_word, status);
                    if (rt11_write_segment(img, seg_block, segbuf) != 0) {
                        return -1;
                    }
                    return 0;
                }
            }
        }

        next_seg = hdr.next_segment;
        guard++;
        if (guard > total_segments + 4) {
            break;
        }
    }

    errno = ENOENT;
    return -1;
}

int rt11_squeeze(rt11_image_t *img)
{
    uint16_t dir_start;
    uint16_t next_seg = 1;
    uint16_t total_segments = 0;
    uint32_t guard = 0;

    if (!img || !img->fp) {
        errno = EINVAL;
        return -1;
    }

    if (rt11_read_home(img, &dir_start) != 0) {
        return -1;
    }

    while (next_seg != 0) {
        uint16_t seg_num = next_seg;
        uint16_t seg_next = 0;

        if (rt11_squeeze_segment(img, dir_start, seg_num, &seg_next) != 0) {
            return -1;
        }

        if (total_segments == 0) {
            uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
            rt11_dirseg_hdr_t hdr;
            uint32_t seg_block =
                (uint32_t)dir_start +
                (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;
            if (rt11_read_segment(img, seg_block, segbuf) != 0) {
                return -1;
            }
            if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
                return -1;
            }
            total_segments = hdr.total_segments;
            if (total_segments == 0) {
                total_segments = 1;
            }
        }

        next_seg = seg_next;
        guard++;
        if (guard > total_segments + 4) {
            errno = EINVAL;
            return -1;
        }
    }

    return 0;
}

static void rt11_home_set_ascii(uint8_t *buf, size_t off, const char *text,
                                size_t len)
{
    size_t i;
    if (len > RT11_HOME_FIELD_LEN) {
        len = RT11_HOME_FIELD_LEN;
    }
    for (i = 0; i < RT11_HOME_FIELD_LEN; i++) {
        buf[off + i] = ' ';
    }
    if (!text) {
        return;
    }
    for (i = 0; i < len && text[i] != '\0'; i++) {
        buf[off + i] = (uint8_t)text[i];
    }
}

static uint16_t rt11_date_now(void)
{
    time_t now = time(NULL);
    struct tm *tm_now;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int base;
    unsigned int age;
    unsigned int year_field;

    if (now == (time_t)-1) {
        return 0;
    }

    tm_now = localtime(&now);
    if (!tm_now) {
        return 0;
    }

    year = (unsigned int)(tm_now->tm_year + 1900);
    month = (unsigned int)(tm_now->tm_mon + 1);
    day = (unsigned int)tm_now->tm_mday;

    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }
    if (year < 1972u || year > 2103u) {
        return 0;
    }

    base = year - 1972u;
    age = base / 32u;
    year_field = base % 32u;
    if (age > 3u) {
        return 0;
    }

    return (uint16_t)((age << 14) | (month << 10) | (day << 5) | year_field);
}

static int rt11_squeeze_segment(rt11_image_t *img, uint16_t dir_start,
                                uint16_t seg_num, uint16_t *next_out)
{
    struct file_entry {
        uint16_t status;
        uint16_t name0;
        uint16_t name1;
        uint16_t ext;
        uint16_t length;
        uint16_t job;
        uint16_t date;
        uint32_t old_start;
        uint16_t *extra;
    };

    rt11_dirseg_hdr_t hdr;
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    uint16_t entry_words = 0;
    uint16_t max_entries = 0;
    uint16_t extra_words = 0;
    uint16_t i;
    uint32_t seg_block;
    uint32_t total_len = 0;
    uint32_t sum_files = 0;
    uint32_t cur_block;
    uint32_t new_block;
    size_t file_count = 0;
    size_t file_index = 0;
    int saw_eos = 0;
    int saw_empty = 0;
    int saw_tent = 0;
    struct file_entry *files = NULL;
    uint16_t *extras = NULL;

    seg_block = (uint32_t)dir_start +
                (uint32_t)(seg_num - 1u) * RT11_DIR_SEGMENT_BLOCKS;

    if (rt11_read_segment(img, seg_block, segbuf) != 0) {
        return -1;
    }
    if (rt11_parse_segment_header(segbuf, &hdr) != 0) {
        return -1;
    }
    if (next_out) {
        *next_out = hdr.next_segment;
    }
    if (rt11_entry_layout(&hdr, &entry_words, &max_entries) != 0) {
        return -1;
    }
    if (entry_words < 7u) {
        errno = EINVAL;
        return -1;
    }

    extra_words = (uint16_t)(entry_words - 7u);
    cur_block = hdr.data_start_block;

    for (i = 0; i < max_entries; i++) {
        size_t base_word = 5u + (size_t)i * entry_words;
        uint16_t status = rt11_get_word(segbuf, base_word);
        uint16_t length = rt11_get_word(segbuf, base_word + 4u);

        if (rt11_entry_is_eos(status)) {
            total_len += length;
            saw_eos = 1;
            break;
        }

        if (rt11_entry_is_empty(status)) {
            saw_empty = 1;
        }
        if (status & RT11_E_TENT) {
            saw_tent = 1;
        }
        if (rt11_entry_is_file(status)) {
            file_count++;
        }

        total_len += length;
        cur_block += length;
    }

    if (!saw_eos) {
        errno = EINVAL;
        return -1;
    }
    if (saw_tent) {
        errno = ENOTSUP;
        return -1;
    }
    if (!saw_empty) {
        return 0;
    }

    if (file_count > 0 && extra_words > 0) {
        extras = (uint16_t *)calloc(file_count * (size_t)extra_words,
                                    sizeof(uint16_t));
        if (!extras) {
            return -1;
        }
    }

    if (file_count > 0) {
        files = (struct file_entry *)calloc(file_count, sizeof(*files));
        if (!files) {
            free(extras);
            return -1;
        }
    }

    cur_block = hdr.data_start_block;
    for (i = 0; i < max_entries; i++) {
        size_t base_word = 5u + (size_t)i * entry_words;
        uint16_t status = rt11_get_word(segbuf, base_word);
        uint16_t length = rt11_get_word(segbuf, base_word + 4u);

        if (rt11_entry_is_eos(status)) {
            break;
        }

        if (rt11_entry_is_file(status)) {
            struct file_entry *ent = &files[file_index];
            ent->status = status;
            ent->name0 = rt11_get_word(segbuf, base_word + 1u);
            ent->name1 = rt11_get_word(segbuf, base_word + 2u);
            ent->ext = rt11_get_word(segbuf, base_word + 3u);
            ent->length = length;
            ent->job = rt11_get_word(segbuf, base_word + 5u);
            ent->date = rt11_get_word(segbuf, base_word + 6u);
            ent->old_start = cur_block;
            ent->extra = NULL;
            if (extras) {
                size_t e;
                ent->extra = extras + file_index * (size_t)extra_words;
                for (e = 0; e < extra_words; e++) {
                    ent->extra[e] =
                        rt11_get_word(segbuf, base_word + 7u + e);
                }
            }
            file_index++;
        }

        cur_block += length;
    }

    new_block = hdr.data_start_block;
    for (i = 0; i < file_count; i++) {
        struct file_entry *ent = &files[i];
        if (ent->length == 0) {
            continue;
        }
        if (ent->old_start != new_block) {
            if (rt11_move_blocks(img, ent->old_start, new_block,
                                 ent->length) != 0) {
                free(extras);
                free(files);
                return -1;
            }
        }
        new_block += ent->length;
        sum_files += ent->length;
    }

    if (sum_files > total_len) {
        free(extras);
        free(files);
        errno = EINVAL;
        return -1;
    }

    {
        size_t header_bytes = 5u * 2u;
        size_t seg_bytes = RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS;
        memset(segbuf + header_bytes, 0, seg_bytes - header_bytes);
    }

    for (i = 0; i < file_count; i++) {
        struct file_entry *ent = &files[i];
        size_t base_word = 5u + (size_t)i * entry_words;
        size_t e;

        rt11_set_word(segbuf, base_word, ent->status);
        rt11_set_word(segbuf, base_word + 1u, ent->name0);
        rt11_set_word(segbuf, base_word + 2u, ent->name1);
        rt11_set_word(segbuf, base_word + 3u, ent->ext);
        rt11_set_word(segbuf, base_word + 4u, ent->length);
        rt11_set_word(segbuf, base_word + 5u, ent->job);
        rt11_set_word(segbuf, base_word + 6u, ent->date);

        for (e = 0; e < extra_words; e++) {
            uint16_t val = ent->extra ? ent->extra[e] : 0;
            rt11_set_word(segbuf, base_word + 7u + e, val);
        }
    }

    {
        size_t eos_word = 5u + (size_t)file_count * entry_words;
        uint32_t remaining = total_len - sum_files;
        size_t e;

        if (remaining > 0xffffu) {
            free(extras);
            free(files);
            errno = ERANGE;
            return -1;
        }

        rt11_set_word(segbuf, eos_word, RT11_E_EOS);
        rt11_set_word(segbuf, eos_word + 1u, 0);
        rt11_set_word(segbuf, eos_word + 2u, 0);
        rt11_set_word(segbuf, eos_word + 3u, 0);
        rt11_set_word(segbuf, eos_word + 4u, (uint16_t)remaining);
        rt11_set_word(segbuf, eos_word + 5u, 0);
        rt11_set_word(segbuf, eos_word + 6u, 0);
        for (e = 0; e < extra_words; e++) {
            rt11_set_word(segbuf, eos_word + 7u + e, 0);
        }
    }

    free(extras);
    free(files);

    if (rt11_write_segment(img, seg_block, segbuf) != 0) {
        return -1;
    }

    return 0;
}

int rt11_mkfs(const char *path, uint32_t total_blocks,
              const rt11_mkfs_opts_t *opts, uint32_t *usable_blocks_out)
{
    uint8_t home[RT11_BLOCK_SIZE];
    uint8_t segbuf[RT11_BLOCK_SIZE * RT11_DIR_SEGMENT_BLOCKS];
    FILE *fp;
    uint16_t dir_start = RT11_DIR_START_BLOCK;
    size_t i;
    uint32_t sum = 0;
    uint32_t total_usable = 0;
    uint32_t partitions;
    uint32_t p;
    rt11_image_t img;
    const char *volid = "RT11A";
    const char *owner = "";
    const char *sysid = "DECRT11A";
    uint16_t segments_override = 0;

    if (!path || total_blocks < dir_start + RT11_DIR_SEGMENT_BLOCKS) {
        errno = EINVAL;
        return -1;
    }
    if (opts) {
        if (opts->volid) {
            volid = opts->volid;
        }
        if (opts->owner) {
            owner = opts->owner;
        }
        if (opts->sysid) {
            sysid = opts->sysid;
        }
        segments_override = opts->segments;
    }
    if (segments_override > RT11_SEGMENTS_MAX) {
        errno = ERANGE;
        return -1;
    }

    partitions =
        (total_blocks + RT11_PARTITION_BLOCKS - 1u) / RT11_PARTITION_BLOCKS;
    if (partitions == 0) {
        partitions = 1;
    }
    if (partitions > RT11_PARTITION_MAX) {
        errno = ERANGE;
        return -1;
    }

    fp = fopen(path, "wb+");
    if (!fp) {
        return -1;
    }

    {
        off_t end = (off_t)total_blocks * RT11_BLOCK_SIZE;
        if (end > 0) {
            if (fseeko(fp, end - 1, SEEK_SET) != 0) {
                fclose(fp);
                return -1;
            }
            if (fputc(0, fp) == EOF) {
                fclose(fp);
                return -1;
            }
        }
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) {
        return -1;
    }
    fp = NULL;

    if (rt11_open_image(&img, path, "rb+") != 0) {
        return -1;
    }

    for (p = 0; p < partitions; p++) {
        uint32_t part_blocks =
            total_blocks - (uint32_t)p * RT11_PARTITION_BLOCKS;
        uint16_t total_segments;
        uint16_t max_segments;
        uint32_t dir_blocks;
        uint32_t data_start;
        uint32_t data_blocks;
        uint32_t remaining;
        uint32_t base;
        uint16_t seg;

        if (part_blocks > RT11_PARTITION_BLOCKS) {
            part_blocks = RT11_PARTITION_BLOCKS;
        }
        if (part_blocks < dir_start + RT11_DIR_SEGMENT_BLOCKS) {
            rt11_close_image(&img);
            errno = ENOSPC;
            return -1;
        }

        if (rt11_set_partition(&img, p) != 0) {
            rt11_close_image(&img);
            return -1;
        }

        if (img.volume_blocks <= dir_start + RT11_DIR_SEGMENT_BLOCKS) {
            rt11_close_image(&img);
            errno = ENOSPC;
            return -1;
        }

        max_segments = (uint16_t)((img.volume_blocks - dir_start - 1u) /
                                  RT11_DIR_SEGMENT_BLOCKS);
        if (max_segments == 0) {
            max_segments = 1;
        }

        if (segments_override) {
            total_segments = segments_override;
            if (total_segments > max_segments) {
                rt11_close_image(&img);
                errno = ENOSPC;
                return -1;
            }
        } else {
            total_segments = rt11_default_segments(img.volume_blocks);
            if (total_segments > RT11_SEGMENTS_MAX) {
                total_segments = RT11_SEGMENTS_MAX;
            }
            if (total_segments > max_segments) {
                total_segments = max_segments;
            }
        }

        dir_blocks = (uint32_t)total_segments * RT11_DIR_SEGMENT_BLOCKS;
        data_start = dir_start + dir_blocks;
        if (img.volume_blocks <= data_start) {
            rt11_close_image(&img);
            errno = ENOSPC;
            return -1;
        }

        data_blocks = img.volume_blocks - data_start;
        total_usable += data_blocks;

        memset(home, 0, sizeof(home));
        rt11_set_word(home, RT11_HOME_CLUSTER_OFF / 2u, 1);
        rt11_set_word(home, RT11_HOME_DIR_START_OFF / 2u, dir_start);
        rt11_set_word(home, RT11_HOME_SYSVER_OFF / 2u,
                      rt11_rad50_encode3("V3A"));

        rt11_home_set_ascii(home, RT11_HOME_VOLID_OFF, volid,
                            RT11_HOME_FIELD_LEN);
        rt11_home_set_ascii(home, RT11_HOME_OWNER_OFF, owner,
                            RT11_HOME_FIELD_LEN);
        rt11_home_set_ascii(home, RT11_HOME_SYSID_OFF, sysid,
                            RT11_HOME_FIELD_LEN);

        rt11_set_word(home, RT11_HOME_CKSUM_OFF / 2u, 0);
        sum = 0;
        for (i = 0; i < RT11_BLOCK_SIZE / 2u; i++) {
            if (i == RT11_HOME_CKSUM_OFF / 2u) {
                continue;
            }
            sum = (sum + rt11_get_word(home, i)) & 0xffffu;
        }
        rt11_set_word(home, RT11_HOME_CKSUM_OFF / 2u, (uint16_t)sum);

        if (rt11_write_block(&img, 1, home) != 0) {
            rt11_close_image(&img);
            return -1;
        }

        remaining = data_blocks;
        base = data_start;

        for (seg = 0; seg < total_segments; seg++) {
            uint32_t seg_left = (uint32_t)total_segments - seg;
            uint32_t seg_len =
                seg_left ? (remaining + seg_left - 1u) / seg_left : remaining;
            uint32_t seg_block = (uint32_t)dir_start +
                                 (uint32_t)seg * RT11_DIR_SEGMENT_BLOCKS;
            uint16_t next_seg =
                (seg + 1u < total_segments) ? (uint16_t)(seg + 2u) : 0;

            if (seg_len > 0xffffu) {
                rt11_close_image(&img);
                errno = ERANGE;
                return -1;
            }

            memset(segbuf, 0, sizeof(segbuf));
            rt11_set_word(segbuf, 0, total_segments);
            rt11_set_word(segbuf, 1, next_seg);
            rt11_set_word(segbuf, 2, total_segments);
            rt11_set_word(segbuf, 3, 0);
            rt11_set_word(segbuf, 4, (uint16_t)base);
            rt11_write_entry(segbuf, 5u, 7u, RT11_E_EOS, NULL,
                             (uint16_t)seg_len, 0, 0);

            if (rt11_write_segment(&img, seg_block, segbuf) != 0) {
                rt11_close_image(&img);
                return -1;
            }

            base += seg_len;
            remaining -= seg_len;
        }
    }

    rt11_close_image(&img);

    if (usable_blocks_out) {
        *usable_blocks_out = total_usable;
    }

    return 0;
}
