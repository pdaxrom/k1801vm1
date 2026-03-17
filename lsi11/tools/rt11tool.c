#include "rt11fs.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define RK05_BLOCKS 4872u
#define RL01_BLOCKS 10240u
#define RL02_BLOCKS 20480u
#define RP06_BLOCKS 340670u
#define RP07_BLOCKS 1008000u

#define RT11_DIR_START_BLOCK 6u
#define RT11_DIR_SEGMENT_BLOCKS 2u
#define RT11_SEGMENTS_SMALL 16u
#define RT11_SEGMENTS_MAX 31u
#define RT11_SEGMENTS_THRESHOLD 10000u
#define RT11_PARTITION_BLOCKS 65536u
#define RT11_PARTITION_USABLE 65535u
#define RT11_PARTITION_MAX 256u
#define RT11_HOME_CLUSTER_OFF 0722
#define RT11_HOME_DIR_START_OFF 0724
#define RT11_HOME_SYSVER_OFF 0726
#define RT11_HOME_VOLID_OFF 0730
#define RT11_HOME_OWNER_OFF 0744
#define RT11_HOME_SYSID_OFF 0760
#define RT11_HOME_FIELD_LEN 12u

static uint16_t default_segments(uint32_t total_blocks)
{
    if (total_blocks <= RT11_SEGMENTS_THRESHOLD) {
        return RT11_SEGMENTS_SMALL;
    }
    return RT11_SEGMENTS_MAX;
}

static uint32_t calc_partition_usable(uint32_t part_blocks,
                                      uint16_t segments_override)
{
    uint32_t volume_blocks = part_blocks;
    uint16_t segs;
    uint16_t max_segments;
    uint32_t data_start;

    if (volume_blocks >= RT11_PARTITION_BLOCKS) {
        volume_blocks = RT11_PARTITION_USABLE;
    }
    if (volume_blocks <= RT11_DIR_START_BLOCK + RT11_DIR_SEGMENT_BLOCKS) {
        return 0;
    }

    segs = segments_override;
    if (segs == 0) {
        segs = default_segments(volume_blocks);
        if (segs > RT11_SEGMENTS_MAX) {
            segs = RT11_SEGMENTS_MAX;
        }
    }

    max_segments = (uint16_t)((volume_blocks - RT11_DIR_START_BLOCK - 1u) /
                              RT11_DIR_SEGMENT_BLOCKS);
    if (max_segments == 0) {
        max_segments = 1;
    }
    if (segs > max_segments) {
        segs = max_segments;
    }

    data_start =
        RT11_DIR_START_BLOCK + (uint32_t)segs * RT11_DIR_SEGMENT_BLOCKS;
    if (volume_blocks <= data_start) {
        return 0;
    }

    return volume_blocks - data_start;
}

static uint32_t calc_total_usable(uint32_t total_blocks,
                                  uint16_t segments_override)
{
    uint32_t partitions =
        (total_blocks + RT11_PARTITION_BLOCKS - 1u) / RT11_PARTITION_BLOCKS;
    uint32_t total = 0;
    uint32_t p;

    if (partitions == 0) {
        partitions = 1;
    }

    for (p = 0; p < partitions; p++) {
        uint32_t part_blocks =
            total_blocks - p * RT11_PARTITION_BLOCKS;
        if (part_blocks > RT11_PARTITION_BLOCKS) {
            part_blocks = RT11_PARTITION_BLOCKS;
        }
        total += calc_partition_usable(part_blocks, segments_override);
    }

    return total;
}

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: rt11tool <command> [args]\n"
            "Commands:\n"
            "  ls <image> [--partition N] [--long] [--debug]\n"
            "  info <image> [--partition N]\n"
            "  bootblock <image> [--partition N]\n"
            "  extract <image> <outdir> [NAME.EXT] [--lower] [--partition N]\n"
            "  add <image> <hostfile> <NAME.EXT> [--partition N]\n"
            "  add <image> --dir <dir> [--partition N]\n"
            "  rm <image> <NAME.EXT> [--partition N] [--force]\n"
            "  mkfs <image> (--blocks N | --rk05 | --rl02 | --rp06 | --rp07)\n"
            "       [--segments N] [--volid TEXT] [--owner TEXT] [--sysid TEXT]\n");
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static uint16_t get_word_le(const uint8_t *buf, size_t word_index)
{
    size_t off = word_index * 2u;
    return (uint16_t)(buf[off] | ((uint16_t)buf[off + 1] << 8));
}

static void rad50_decode3(uint16_t w, char out[4])
{
    static const char table[40] =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";
    unsigned int c0 = (unsigned int)(w / 1600u);
    unsigned int rem = (unsigned int)(w % 1600u);
    unsigned int c1 = rem / 40u;
    unsigned int c2 = rem % 40u;

    out[0] = table[c0 < 40 ? c0 : 0];
    out[1] = table[c1 < 40 ? c1 : 0];
    out[2] = table[c2 < 40 ? c2 : 0];
    out[3] = '\0';
}

static void rt11_date_to_string(uint16_t word, char *out, size_t out_size)
{
    unsigned int age;
    unsigned int month;
    unsigned int day;
    unsigned int year;

    if (out_size == 0) {
        return;
    }

    if (word == 0) {
        snprintf(out, out_size, "-");
        return;
    }

    age = (unsigned int)((word >> 14) & 0x3u);
    month = (unsigned int)((word >> 10) & 0xfu);
    day = (unsigned int)((word >> 5) & 0x1fu);
    year = (unsigned int)(word & 0x1fu);

    if (month < 1 || month > 12 || day < 1 || day > 31) {
        snprintf(out, out_size, "INVALID");
        return;
    }

    year = 1972u + age * 32u + year;
    snprintf(out, out_size, "%04u-%02u-%02u", year, month, day);
}

static void rt11_status_to_text(uint16_t status, char *type_out,
                                size_t type_size, char *flags_out,
                                size_t flags_size)
{
    const char *type = "UNK";
    char flags[4];
    size_t pos = 0;

    if (status & RT11_E_EOS) {
        type = "EOS";
    } else if (status & RT11_E_MPTY) {
        type = "MPTY";
    } else if (status & RT11_E_TENT) {
        type = "TENT";
    } else if (status & RT11_E_PERM) {
        type = "PERM";
    }

    if (status & RT11_E_READ) {
        flags[pos++] = 'R';
    }
    if (status & RT11_E_PROT) {
        flags[pos++] = 'P';
    }
    if (pos == 0) {
        flags[pos++] = '-';
    }
    flags[pos] = '\0';

    snprintf(type_out, type_size, "%s", type);
    snprintf(flags_out, flags_size, "%s", flags);
}

static void home_get_field(const uint8_t *buf, size_t off, char *out,
                           size_t out_size)
{
    size_t i;
    size_t len = 0;

    if (out_size == 0) {
        return;
    }

    for (i = 0; i < RT11_HOME_FIELD_LEN && len + 1 < out_size; i++) {
        out[len++] = (char)buf[off + i];
    }
    while (len > 0 && out[len - 1] == ' ') {
        len--;
    }
    out[len] = '\0';
}

static int parse_partition_value(const char *s, uint32_t *partition_out)
{
    char *end = NULL;
    unsigned long v;

    v = strtoul(s, &end, 0);
    if (!end || *end != '\0') {
        return -1;
    }
    if (v >= RT11_PARTITION_MAX) {
        return -1;
    }
    *partition_out = (uint32_t)v;
    return 0;
}

static int read_block(rt11_image_t *img, uint32_t block, uint8_t *buf)
{
    uint32_t abs_block;

    if (block >= img->volume_blocks) {
        errno = EINVAL;
        return -1;
    }
    abs_block = img->base_block + block;
    if (abs_block >= img->total_blocks) {
        errno = EINVAL;
        return -1;
    }
    if (fseeko(img->fp, (off_t)abs_block * RT11_BLOCK_SIZE, SEEK_SET) != 0) {
        return -1;
    }
    if (fread(buf, 1, RT11_BLOCK_SIZE, img->fp) != RT11_BLOCK_SIZE) {
        return -1;
    }
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    if (argc < 1) {
        usage(stderr);
        return 1;
    }
    const char *image = argv[0];
    uint32_t partition = 0;
    int have_partition = 0;
    rt11_image_t img;
    uint8_t home[RT11_BLOCK_SIZE];
    uint16_t dir_start;
    uint16_t cluster;
    uint16_t sysver;
    char sysver_str[4];
    char volid[RT11_HOME_FIELD_LEN + 1u];
    char owner[RT11_HOME_FIELD_LEN + 1u];
    char sysid[RT11_HOME_FIELD_LEN + 1u];
    uint16_t seg_num = 1;
    uint16_t total_segments = 0;
    uint16_t highest_segment = 0;
    uint16_t extra_bytes = 0;
    uint32_t guard = 0;

    if (argc > 1) {
        if (argc != 3 || strcmp(argv[1], "--partition") != 0) {
            usage(stderr);
            return 1;
        }
        if (parse_partition_value(argv[2], &partition) != 0) {
            fprintf(stderr, "rt11tool: invalid partition\n");
            return 1;
        }
        have_partition = 1;
    }

    if (rt11_open_image(&img, image, "rb") != 0) {
        perror("rt11tool: open image");
        return 1;
    }

    if (have_partition && rt11_set_partition(&img, partition) != 0) {
        perror("rt11tool: partition");
        rt11_close_image(&img);
        return 1;
    }

    if (read_block(&img, 1, home) != 0) {
        perror("rt11tool: read home block");
        rt11_close_image(&img);
        return 1;
    }

    cluster = get_word_le(home, RT11_HOME_CLUSTER_OFF / 2u);
    dir_start = get_word_le(home, RT11_HOME_DIR_START_OFF / 2u);
    sysver = get_word_le(home, RT11_HOME_SYSVER_OFF / 2u);
    if (cluster == 0) {
        cluster = 1;
    }
    if (dir_start == 0) {
        dir_start = RT11_DIR_START_BLOCK;
    }
    rad50_decode3(sysver, sysver_str);
    home_get_field(home, RT11_HOME_VOLID_OFF, volid, sizeof(volid));
    home_get_field(home, RT11_HOME_OWNER_OFF, owner, sizeof(owner));
    home_get_field(home, RT11_HOME_SYSID_OFF, sysid, sizeof(sysid));

    {
        uint32_t total_parts =
            (img.total_blocks + RT11_PARTITION_BLOCKS - 1u) /
            RT11_PARTITION_BLOCKS;
        if (total_parts == 0) {
            total_parts = 1;
        }
        printf("Image blocks: %u (octal %o)\n", img.total_blocks,
               img.total_blocks);
        printf("Partitions: %u\n", total_parts);
    }
    printf("Partition: %u (base block %o)\n", img.partition_number,
           img.base_block);
    printf("Partition blocks: %u (octal %o)\n", img.partition_blocks,
           img.partition_blocks);
    printf("Usable RT-11 blocks: %u (octal %o)\n", img.volume_blocks,
           img.volume_blocks);
    printf("Directory start block: %o\n", dir_start);
    printf("Pack cluster size: %o\n", cluster);
    printf("System version: %s\n", sysver_str);
    printf("Volume ID: %s\n", volid);
    printf("Owner: %s\n", owner);
    printf("System ID: %s\n", sysid);

    while (seg_num != 0) {
        uint8_t segbuf[RT11_BLOCK_SIZE];
        uint32_t seg_block =
            (uint32_t)dir_start + (uint32_t)(seg_num - 1u) * 2u;
        uint16_t seg_total;
        uint16_t seg_next;
        uint16_t seg_high;
        uint16_t seg_extra;
        uint16_t seg_data;

        if (read_block(&img, seg_block, segbuf) != 0) {
            perror("rt11tool: read segment");
            rt11_close_image(&img);
            return 1;
        }

        seg_total = get_word_le(segbuf, 0);
        seg_next = get_word_le(segbuf, 1);
        seg_high = get_word_le(segbuf, 2);
        seg_extra = get_word_le(segbuf, 3);
        seg_data = get_word_le(segbuf, 4);

        if (total_segments == 0) {
            total_segments = seg_total;
            highest_segment = seg_high;
            extra_bytes = seg_extra;
            printf("Directory segments: total %o, highest %o, extra bytes %o\n",
                   total_segments, highest_segment, extra_bytes);
            printf("Segments:\n");
        }

        printf("  %o: next %o, data_start %o\n", seg_num, seg_next, seg_data);

        seg_num = seg_next;
        guard++;
        if (guard > 256) {
            break;
        }
    }

    rt11_close_image(&img);
    return 0;
}

static int cmd_bootblock(int argc, char **argv)
{
    if (argc < 1) {
        usage(stderr);
        return 1;
    }
    const char *image = argv[0];
    uint32_t partition = 0;
    int have_partition = 0;
    rt11_image_t img;
    uint8_t buf[RT11_BLOCK_SIZE];
    uint32_t i;

    if (argc > 1) {
        if (argc != 3 || strcmp(argv[1], "--partition") != 0) {
            usage(stderr);
            return 1;
        }
        if (parse_partition_value(argv[2], &partition) != 0) {
            fprintf(stderr, "rt11tool: invalid partition\n");
            return 1;
        }
        have_partition = 1;
    }

    if (rt11_open_image(&img, image, "rb") != 0) {
        perror("rt11tool: open image");
        return 1;
    }

    if (have_partition && rt11_set_partition(&img, partition) != 0) {
        perror("rt11tool: partition");
        rt11_close_image(&img);
        return 1;
    }

    if (read_block(&img, 0, buf) != 0) {
        perror("rt11tool: read boot block");
        rt11_close_image(&img);
        return 1;
    }

    for (i = 0; i < RT11_BLOCK_SIZE; i += 16) {
        uint32_t j;
        printf("%06o: ", i);
        for (j = 0; j < 16; j++) {
            printf("%02x ", buf[i + j]);
        }
        printf("\n");
    }

    rt11_close_image(&img);
    return 0;
}

static int build_out_path(char *out, size_t out_size, const char *dir,
                          const char *name)
{
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/') {
        int written = snprintf(out, out_size, "%s%s", dir, name);
        return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
    }
    {
        int written = snprintf(out, out_size, "%s/%s", dir, name);
        return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
    }
}

static int add_dir(rt11_image_t *img, const char *dir)
{
    DIR *dp;
    struct dirent *de;
    int rc = 0;

    dp = opendir(dir);
    if (!dp) {
        perror("rt11tool: opendir");
        return 1;
    }

    while ((de = readdir(dp)) != NULL) {
        char path[PATH_MAX];
        struct stat st;
        rt11_name_t name;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if (build_out_path(path, sizeof(path), dir, de->d_name) != 0) {
            fprintf(stderr, "rt11tool: path too long: %s\n", de->d_name);
            rc = 1;
            continue;
        }

        if (stat(path, &st) != 0) {
            fprintf(stderr, "rt11tool: stat %s: %s\n", de->d_name,
                    strerror(errno));
            rc = 1;
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        if (rt11_name_from_host(de->d_name, &name) != 0) {
            fprintf(stderr, "rt11tool: invalid RT-11 name: %s\n",
                    de->d_name);
            rc = 1;
            continue;
        }

        if (rt11_add_file(img, path, &name) != 0) {
            fprintf(stderr, "rt11tool: add %s: %s\n", de->d_name,
                    strerror(errno));
            rc = 1;
            if (errno == ENOSPC) {
                break;
            }
        }
    }

    closedir(dp);
    return rc;
}

static int cmd_ls(int argc, char **argv)
{
    if (argc < 1) {
        usage(stderr);
        return 1;
    }
    const char *image = argv[0];
    uint32_t partition = 0;
    int have_partition = 0;
    int long_format = 0;
    int debug = 0;
    rt11_image_t img;
    rt11_dirlist_t list;
    int argi;
    size_t i;

    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--partition") == 0 && argi + 1 < argc) {
            if (parse_partition_value(argv[argi + 1], &partition) != 0) {
                fprintf(stderr, "rt11tool: invalid partition\n");
                return 1;
            }
            have_partition = 1;
            argi++;
        } else if (strcmp(argv[argi], "--long") == 0) {
            long_format = 1;
        } else if (strcmp(argv[argi], "--debug") == 0) {
            debug = 1;
        } else {
            usage(stderr);
            return 1;
        }
    }

    if (rt11_open_image(&img, image, "rb") != 0) {
        perror("rt11tool: open image");
        return 1;
    }

    if (have_partition && rt11_set_partition(&img, partition) != 0) {
        perror("rt11tool: partition");
        rt11_close_image(&img);
        return 1;
    }

    if (rt11_read_directory(&img, &list) != 0) {
        perror("rt11tool: read directory");
        rt11_close_image(&img);
        return 1;
    }

    if (long_format) {
        if (debug) {
            printf("%-12s %6s %6s %8s %-5s %-5s %6s %-10s %6s %6s\n",
                   "NAME.EXT", "start", "length", "bytes", "stype", "flags",
                   "status", "job", "date", "dateo");
        } else {
            printf("%-12s %6s %6s %8s %-5s %-5s %6s %-10s\n", "NAME.EXT",
                   "start", "length", "bytes", "stype", "flags", "job",
                   "date");
        }
    } else if (debug) {
        printf("%-12s %6s %6s %6s\n", "NAME.EXT", "start", "length", "status");
    } else {
        printf("%-12s %6s %6s %-5s %-5s\n", "NAME.EXT", "start", "length",
               "stype", "flags");
    }
    for (i = 0; i < list.count; i++) {
        char fname[16];
        char date_str[16];
        char stype[8];
        char sflags[8];
        uint32_t bytes = (uint32_t)list.entries[i].length * RT11_BLOCK_SIZE;
        if (list.entries[i].ext[0]) {
            snprintf(fname, sizeof(fname), "%s.%s", list.entries[i].name,
                     list.entries[i].ext);
        } else {
            snprintf(fname, sizeof(fname), "%s", list.entries[i].name);
        }
        if (long_format) {
            rt11_date_to_string(list.entries[i].date, date_str,
                                sizeof(date_str));
            rt11_status_to_text(list.entries[i].status, stype, sizeof(stype),
                                sflags, sizeof(sflags));
            if (debug) {
                printf("%-12s %06o %06o %8u %-5s %-5s %06o %06o %-10s %06o\n",
                       fname, list.entries[i].start_block,
                       list.entries[i].length, bytes, stype, sflags,
                       list.entries[i].status, list.entries[i].job, date_str,
                       list.entries[i].date);
            } else {
                printf("%-12s %06o %06o %8u %-5s %-5s %06o %-10s\n", fname,
                       list.entries[i].start_block, list.entries[i].length,
                       bytes, stype, sflags, list.entries[i].job, date_str);
            }
        } else if (debug) {
            printf("%-12s %06o %06o %06o\n", fname, list.entries[i].start_block,
                   list.entries[i].length, list.entries[i].status);
        } else {
            rt11_status_to_text(list.entries[i].status, stype, sizeof(stype),
                                sflags, sizeof(sflags));
            printf("%-12s %06o %06o %-5s %-5s\n", fname,
                   list.entries[i].start_block, list.entries[i].length, stype,
                   sflags);
        }
    }

    rt11_free_dirlist(&list);
    rt11_close_image(&img);
    return 0;
}

static int cmd_extract(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    const char *image = argv[0];
    const char *outdir = argv[1];
    const char *name_arg = NULL;
    int lower = 0;
    uint32_t partition = 0;
    int have_partition = 0;
    int i;
    rt11_image_t img;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--lower") == 0) {
            lower = 1;
        } else if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc) {
            if (parse_partition_value(argv[i + 1], &partition) != 0) {
                fprintf(stderr, "rt11tool: invalid partition\n");
                return 1;
            }
            have_partition = 1;
            i++;
        } else if (!name_arg) {
            name_arg = argv[i];
        } else {
            usage(stderr);
            return 1;
        }
    }

    if (ensure_dir(outdir) != 0) {
        perror("rt11tool: output dir");
        return 1;
    }

    if (rt11_open_image(&img, image, "rb") != 0) {
        perror("rt11tool: open image");
        return 1;
    }

    if (have_partition && rt11_set_partition(&img, partition) != 0) {
        perror("rt11tool: partition");
        rt11_close_image(&img);
        return 1;
    }

    if (name_arg) {
        rt11_name_t name;
        rt11_dirent_t ent;
        char fname[32];
        char path[512];

        if (rt11_name_from_host(name_arg, &name) != 0) {
            fprintf(stderr, "rt11tool: invalid RT-11 name\n");
            rt11_close_image(&img);
            return 1;
        }

        if (rt11_find_file(&img, &name, &ent) != 0) {
            perror("rt11tool: find file");
            rt11_close_image(&img);
            return 1;
        }

        {
            rt11_name_t nm;
            nm.name_words[0] = name.name_words[0];
            nm.name_words[1] = name.name_words[1];
            nm.ext_word = name.ext_word;
            rt11_name_to_host(&nm, fname, sizeof(fname), lower);
        }

        if (build_out_path(path, sizeof(path), outdir, fname) != 0) {
            rt11_close_image(&img);
            return 1;
        }

        if (rt11_extract_file(&img, &ent, path) != 0) {
            perror("rt11tool: extract");
            rt11_close_image(&img);
            return 1;
        }
    } else {
        rt11_dirlist_t list;
        if (rt11_read_directory(&img, &list) != 0) {
            perror("rt11tool: read directory");
            rt11_close_image(&img);
            return 1;
        }

        for (i = 0; i < (int)list.count; i++) {
            char fname[32];
            char path[512];
            size_t j;

            if (list.entries[i].ext[0]) {
                snprintf(fname, sizeof(fname), "%s.%s", list.entries[i].name,
                         list.entries[i].ext);
            } else {
                snprintf(fname, sizeof(fname), "%s", list.entries[i].name);
            }
            if (lower) {
                for (j = 0; fname[j]; j++) {
                    fname[j] = (char)tolower((unsigned char)fname[j]);
                }
            }
            if (build_out_path(path, sizeof(path), outdir, fname) != 0) {
                rt11_free_dirlist(&list);
                rt11_close_image(&img);
                return 1;
            }
            if (rt11_extract_file(&img, &list.entries[i], path) != 0) {
                perror("rt11tool: extract");
                rt11_free_dirlist(&list);
                rt11_close_image(&img);
                return 1;
            }
        }

        rt11_free_dirlist(&list);
    }

    rt11_close_image(&img);
    return 0;
}

static int cmd_add(int argc, char **argv)
{
    const char *image = argv[0];
    const char *host = NULL;
    const char *target = NULL;
    uint32_t partition = 0;
    int have_partition = 0;
    int dir_mode = 0;
    int i;
    rt11_image_t img;
    rt11_name_t name;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc) {
            if (parse_partition_value(argv[i + 1], &partition) != 0) {
                fprintf(stderr, "rt11tool: invalid partition\n");
                return 1;
            }
            have_partition = 1;
            i++;
        } else if (strcmp(argv[i], "--dir") == 0) {
            dir_mode = 1;
            if (i + 1 < argc) {
                host = argv[i + 1];
                i++;
            } else {
                usage(stderr);
                return 1;
            }
        } else if (!host) {
            host = argv[i];
        } else if (!target) {
            target = argv[i];
        } else {
            usage(stderr);
            return 1;
        }
    }

    if (!host) {
        usage(stderr);
        return 1;
    }
    if (dir_mode && target) {
        usage(stderr);
        return 1;
    }
    if (!dir_mode && !target) {
        usage(stderr);
        return 1;
    }

    if (rt11_open_image(&img, image, "rb+") != 0) {
        perror("rt11tool: open image");
        return 1;
    }

    if (have_partition && rt11_set_partition(&img, partition) != 0) {
        perror("rt11tool: partition");
        rt11_close_image(&img);
        return 1;
    }

    if (dir_mode) {
        if (ensure_dir(host) != 0) {
            perror("rt11tool: dir");
            rt11_close_image(&img);
            return 1;
        }
        if (add_dir(&img, host) != 0) {
            rt11_close_image(&img);
            return 1;
        }
    } else {
        if (rt11_name_from_host(target, &name) != 0) {
            fprintf(stderr, "rt11tool: invalid RT-11 name\n");
            rt11_close_image(&img);
            return 1;
        }
        if (rt11_add_file(&img, host, &name) != 0) {
            perror("rt11tool: add");
            rt11_close_image(&img);
            return 1;
        }
    }

    rt11_close_image(&img);
    return 0;
}

static int cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    const char *image = argv[0];
    const char *target = argv[1];
    uint32_t partition = 0;
    int have_partition = 0;
    int force = 0;
    int i;
    rt11_image_t img;
    rt11_name_t name;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc) {
            if (parse_partition_value(argv[i + 1], &partition) != 0) {
                fprintf(stderr, "rt11tool: invalid partition\n");
                return 1;
            }
            have_partition = 1;
            i++;
        } else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else {
            usage(stderr);
            return 1;
        }
    }

    if (rt11_name_from_host(target, &name) != 0) {
        fprintf(stderr, "rt11tool: invalid RT-11 name\n");
        return 1;
    }

    if (rt11_open_image(&img, image, "rb+") != 0) {
        perror("rt11tool: open image");
        return 1;
    }

    if (have_partition && rt11_set_partition(&img, partition) != 0) {
        perror("rt11tool: partition");
        rt11_close_image(&img);
        return 1;
    }

    if (rt11_remove_file(&img, &name, force) != 0) {
        perror("rt11tool: rm");
        rt11_close_image(&img);
        return 1;
    }

    rt11_close_image(&img);
    return 0;
}

static int cmd_mkfs(int argc, char **argv)
{
    const char *image = argv[0];
    uint32_t blocks = 0;
    rt11_mkfs_opts_t opts;
    int i;

    memset(&opts, 0, sizeof(opts));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--blocks") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long v = strtoul(argv[i + 1], &end, 0);
            if (!end || *end != '\0') {
                fprintf(stderr, "rt11tool: invalid block count\n");
                return 1;
            }
            blocks = (uint32_t)v;
            i++;
        } else if (strcmp(argv[i], "--segments") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long v = strtoul(argv[i + 1], &end, 0);
            if (!end || *end != '\0' || v == 0 ||
                v > RT11_SEGMENTS_MAX) {
                fprintf(stderr, "rt11tool: invalid segment count\n");
                return 1;
            }
            opts.segments = (uint16_t)v;
            i++;
        } else if (strcmp(argv[i], "--volid") == 0 && i + 1 < argc) {
            opts.volid = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--owner") == 0 && i + 1 < argc) {
            opts.owner = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--sysid") == 0 && i + 1 < argc) {
            opts.sysid = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--rk05") == 0) {
            blocks = RK05_BLOCKS;
        } else if (strcmp(argv[i], "--rl01") == 0) {
            blocks = RL01_BLOCKS;
        } else if (strcmp(argv[i], "--rl02") == 0) {
            blocks = RL02_BLOCKS;
        } else if (strcmp(argv[i], "--rp06") == 0) {
            blocks = RP06_BLOCKS;
        } else if (strcmp(argv[i], "--rp07") == 0) {
            blocks = RP07_BLOCKS;
        } else {
            usage(stderr);
            return 1;
        }
    }

    if (blocks == 0) {
        usage(stderr);
        return 1;
    }

    {
        uint32_t usable = 0;
        uint32_t expected = calc_total_usable(blocks, opts.segments);
        if (rt11_mkfs(image, blocks, &opts, &usable) != 0) {
            perror("rt11tool: mkfs");
            return 1;
        }
        if (usable < expected) {
            fprintf(stderr,
                    "rt11tool: warning: usable RT-11 space capped at %u blocks\n",
                    usable);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "ls") == 0) {
        if (argc < 3) {
            usage(stderr);
            return 1;
        }
        return cmd_ls(argc - 2, &argv[2]);
    }

    if (strcmp(argv[1], "extract") == 0) {
        if (argc < 4) {
            usage(stderr);
            return 1;
        }
        return cmd_extract(argc - 2, &argv[2]);
    }

    if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            usage(stderr);
            return 1;
        }
        return cmd_info(argc - 2, &argv[2]);
    }

    if (strcmp(argv[1], "bootblock") == 0) {
        if (argc < 3) {
            usage(stderr);
            return 1;
        }
        return cmd_bootblock(argc - 2, &argv[2]);
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 5) {
            usage(stderr);
            return 1;
        }
        return cmd_add(argc - 2, &argv[2]);
    }

    if (strcmp(argv[1], "rm") == 0) {
        if (argc < 4) {
            usage(stderr);
            return 1;
        }
        return cmd_rm(argc - 2, &argv[2]);
    }

    if (strcmp(argv[1], "mkfs") == 0) {
        if (argc < 4) {
            usage(stderr);
            return 1;
        }
        return cmd_mkfs(argc - 2, &argv[2]);
    }

    usage(stderr);
    return 1;
}
