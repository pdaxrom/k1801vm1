#include "rsx11fs.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint32_t bytes;
    uint16_t list_block_size;
    const char *desc;
} rsx11_disk_type_t;

static const rsx11_disk_type_t rsx11_disk_types[] = {
    {"rk05", 2494464u, 512u, "RK11 RK05"},
    {"rk06", 13888512u, 512u, "RH11 RK06"},
    {"rk07", 27540480u, 512u, "RH11 RK07"},
    {"rl01", 5242880u, 256u, "RL11 RL01"},
    {"rl02", 10485760u, 256u, "RL11 RL02"},
    {"rm05", 256196608u, 512u, "XP/RP RM05"},
    {"rd31", 21278720u, 512u, "RQ RD31"},
    {"rd32", 42600448u, 512u, "RQ RD32"},
    {"rd51", 11059200u, 512u, "RQ RD51"},
    {"rd52", 30965760u, 512u, "RQ RD52"},
    {"rd53", 71000064u, 512u, "RQ RD53"},
    {"rd54", 159334400u, 512u, "RQ RD54"},
    {"ra60", 204890112u, 512u, "RQ RA60"},
    {"ra70", 280084992u, 512u, "RQ RA70"},
    {"ra71", 700062720u, 512u, "RQ RA71"},
    {"ra80", 121452544u, 512u, "RQ RA80"},
    {"ra81", 456228864u, 512u, "RQ RA81"},
    {"ra82", 622932480u, 512u, "RQ RA82"},
    {"rx50", 409600u, 512u, "RQ RX50"},
    {"rx33", 1228800u, 512u, "RQ RX33"},
};

static const rsx11_disk_type_t *rsx11_find_disk_type(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(rsx11_disk_types) / sizeof(rsx11_disk_types[0]);
         i++) {
        if (strcmp(rsx11_disk_types[i].name, name) == 0) {
            return &rsx11_disk_types[i];
        }
    }
    return NULL;
}

static void rsx11_list_disk_types(FILE *out)
{
    size_t i;

    fprintf(out, "Supported types:\n");
    for (i = 0; i < sizeof(rsx11_disk_types) / sizeof(rsx11_disk_types[0]);
         i++) {
        const rsx11_disk_type_t *t = &rsx11_disk_types[i];
        uint32_t blocks = t->bytes / t->list_block_size;

        fprintf(out,
                "  %-4s  disk  %9u bytes (%u blocks of %u)  %s\n",
                t->name,
                t->bytes,
                blocks,
                (unsigned)t->list_block_size,
                t->desc);
    }
}

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: rsx11tool <command> [args]\n"
            "Commands:\n"
            "  info <image>\n"
            "  ls <image> [[MFD:|[grp,user]]NAME.EXT[;ver]]\n"
            "  extract <image> <outdir> [[grp,user]][NAME.EXT[;ver]]\n"
            "  add <image> <hostfile> [[grp,user]]NAME.EXT[;ver]\n"
            "  rm <image> [[grp,user]]NAME.EXT[;ver]\n"
            "  mkdir <image> [grp,user]\n"
            "  rmdir <image> [grp,user]\n"
            "  mkfs --list\n"
            "  mkfs <image> (--blocks N | --type <type>) [--label LABEL] [--boot-from image] [--bootblock file]\n"
            "  bootblock <image> [--write file]\n"
            "  fsck <image> [--repair]\n");
}

static void format_filespec(const rsx11_dirent_t *ent, char *out, size_t out_size)
{
    if (ent->ext[0] != '\0') {
        snprintf(out, out_size, "%s.%s;%u",
                 ent->name, ent->ext, (unsigned)ent->version);
    } else {
        snprintf(out, out_size, "%s;%u",
                 ent->name, (unsigned)ent->version);
    }
}

static void upper_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (dst_size == 0u) {
        return;
    }
    for (i = 0; src[i] != '\0' && i + 1u < dst_size; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static int parse_selector(const char *arg,
                          char *dir_out, size_t dir_out_size,
                          char *spec_out, size_t spec_out_size)
{
    const char *rest = arg;
    const char *end;
    size_t len;

    dir_out[0] = '\0';
    spec_out[0] = '\0';
    if (arg == NULL || arg[0] == '\0') {
        return 0;
    }

    if (strcmp(arg, "MFD") == 0) {
        upper_copy(dir_out, dir_out_size, arg);
        return 0;
    }
    if (strncmp(arg, "MFD:", 4u) == 0) {
        upper_copy(dir_out, dir_out_size, "MFD");
        upper_copy(spec_out, spec_out_size, arg + 4u);
        return 0;
    }
    if (arg[0] == '[') {
        end = strchr(arg, ']');
        if (end == NULL) {
            errno = EINVAL;
            return -1;
        }
        len = (size_t)(end - arg + 1);
        if (len >= dir_out_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(dir_out, arg, len);
        dir_out[len] = '\0';
        for (size_t i = 0; i < len; i++) {
            dir_out[i] = (char)toupper((unsigned char)dir_out[i]);
        }
        rest = end + 1;
    }

    if (*rest != '\0') {
        upper_copy(spec_out, spec_out_size, rest);
    }
    return 0;
}

static int parse_filespec(const char *spec,
                          char *name_out, size_t name_out_size,
                          char *ext_out, size_t ext_out_size,
                          int *have_version_out, uint16_t *version_out)
{
    const char *dot;
    const char *semi;
    size_t name_len;
    size_t ext_len;
    char *endptr;
    unsigned long version;

    if (spec == NULL || spec[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    dot = strchr(spec, '.');
    semi = strchr(spec, ';');
    if (semi != NULL && dot != NULL && semi < dot) {
        errno = EINVAL;
        return -1;
    }

    if (dot != NULL) {
        name_len = (size_t)(dot - spec);
        ext_len = (semi != NULL)
                      ? (size_t)(semi - dot - 1)
                      : strlen(dot + 1);
    } else {
        name_len = (semi != NULL)
                      ? (size_t)(semi - spec)
                      : strlen(spec);
        ext_len = 0u;
    }

    if (name_len == 0u || name_len >= name_out_size || name_len > 9u ||
        ext_len >= ext_out_size || ext_len > 3u) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(name_out, spec, name_len);
    name_out[name_len] = '\0';
    if (dot != NULL) {
        memcpy(ext_out, dot + 1, ext_len);
        ext_out[ext_len] = '\0';
    } else {
        ext_out[0] = '\0';
    }

    *have_version_out = 0;
    *version_out = 0u;
    if (semi == NULL) {
        return 0;
    }

    version = strtoul(semi + 1, &endptr, 10);
    if (semi[1] == '\0' || *endptr != '\0' || version == 0u ||
        version > 0xffffu) {
        errno = EINVAL;
        return -1;
    }
    *have_version_out = 1;
    *version_out = (uint16_t)version;
    return 0;
}

static int match_dir(const rsx11_dirent_t *ent, const char *dir_filter)
{
    if (dir_filter == NULL || dir_filter[0] == '\0') {
        return 1;
    }
    return strcmp(ent->dir, dir_filter) == 0;
}

static int match_spec(const rsx11_dirent_t *ent, const char *spec_filter)
{
    char spec[32];
    char base[24];
    const char *semi;

    if (spec_filter == NULL || spec_filter[0] == '\0') {
        return 1;
    }

    format_filespec(ent, spec, sizeof(spec));
    semi = strchr(spec, ';');
    if (semi != NULL) {
        size_t len = (size_t)(semi - spec);
        if (len >= sizeof(base)) {
            len = sizeof(base) - 1u;
        }
        memcpy(base, spec, len);
        base[len] = '\0';
    } else {
        strncpy(base, spec, sizeof(base) - 1u);
        base[sizeof(base) - 1u] = '\0';
    }

    if (strchr(spec_filter, '*') != NULL || strchr(spec_filter, '?') != NULL) {
        const char *pattern = spec_filter;
        const char *text = strchr(spec_filter, ';') != NULL ? spec : base;
        const char *retry_pattern = NULL;
        const char *retry_text = NULL;

        while (*text != '\0') {
            if (*pattern == '*') {
                retry_pattern = ++pattern;
                retry_text = text;
            } else if (*pattern == '?' || *pattern == *text) {
                pattern++;
                text++;
            } else if (retry_pattern != NULL) {
                pattern = retry_pattern;
                text = ++retry_text;
            } else {
                return 0;
            }
        }
        while (*pattern == '*') {
            pattern++;
        }
        return *pattern == '\0';
    }

    if (strchr(spec_filter, ';') != NULL) {
        return strcmp(spec, spec_filter) == 0;
    }
    return strcmp(base, spec_filter) == 0;
}

static int find_empty_directory_marker(const rsx11_dirent_t *entries, size_t count,
                                       const char *dir_filter)
{
    unsigned int group;
    unsigned int user;
    char dir_name[7];
    size_t i;

    if (dir_filter == NULL || dir_filter[0] == '\0') {
        return 0;
    }
    if (strcmp(dir_filter, "MFD") == 0) {
        return 1;
    }
    if (sscanf(dir_filter, "[%o,%o]", &group, &user) != 2 ||
        group > 0377u || user > 0377u) {
        return 0;
    }
    snprintf(dir_name, sizeof(dir_name), "%03o%03o", group, user);

    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].dir, "MFD") == 0 &&
            strcmp(entries[i].name, dir_name) == 0 &&
            strcmp(entries[i].ext, "DIR") == 0) {
            return 1;
        }
    }
    return 0;
}

static void format_selector(const rsx11_dirent_t *ent,
                            char *out, size_t out_size)
{
    char spec[32];

    format_filespec(ent, spec, sizeof(spec));
    if (strcmp(ent->dir, "MFD") == 0) {
        snprintf(out, out_size, "%s", spec);
    } else {
        snprintf(out, out_size, "%s%s", ent->dir, spec);
    }
}

static int read_bootblock_file(const char *path,
                               uint8_t block[RSX11_BLOCK_SIZE])
{
    FILE *in;
    int extra;

    in = fopen(path, "rb");
    if (in == NULL) {
        return -1;
    }
    memset(block, 0, RSX11_BLOCK_SIZE);
    (void)fread(block, 1, RSX11_BLOCK_SIZE, in);
    if (ferror(in)) {
        fclose(in);
        errno = EIO;
        return -1;
    }
    extra = fgetc(in);
    fclose(in);
    if (extra != EOF) {
        errno = EFBIG;
        return -1;
    }
    return 0;
}

static int install_bootblock_from_image(const char *image_path,
                                        const char *source_path)
{
    rsx11_image_t dst;
    rsx11_image_t src;
    uint8_t block[RSX11_BLOCK_SIZE];
    int rc = -1;

    if (rsx11_open_image(&src, source_path) != 0) {
        return -1;
    }
    if (rsx11_read_boot_block(&src, block) != 0) {
        rsx11_close_image(&src);
        return -1;
    }
    rsx11_close_image(&src);

    if (rsx11_open_image_rw(&dst, image_path) != 0) {
        return -1;
    }
    if (rsx11_write_boot_block(&dst, block) == 0) {
        rc = 0;
    }
    rsx11_close_image(&dst);
    return rc;
}

static int install_bootblock_from_file(const char *image_path,
                                       const char *boot_path)
{
    rsx11_image_t dst;
    uint8_t block[RSX11_BLOCK_SIZE];
    int rc = -1;

    if (read_bootblock_file(boot_path, block) != 0) {
        return -1;
    }
    if (rsx11_open_image_rw(&dst, image_path) != 0) {
        return -1;
    }
    if (rsx11_write_boot_block(&dst, block) == 0) {
        rc = 0;
    }
    rsx11_close_image(&dst);
    return rc;
}

static int cmd_info(const char *image_path)
{
    rsx11_image_t img;
    rsx11_home_t home;

    if (rsx11_open_image(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_read_home(&img, &home) != 0) {
        perror("home block");
        rsx11_close_image(&img);
        return 1;
    }

    printf("Format:         Files-11 ODS-1\n");
    printf("Image size:     %llu bytes\n",
           (unsigned long long)img.size_bytes);
    printf("Block size:     %u\n", RSX11_BLOCK_SIZE);
    printf("Total blocks:   %u\n", img.total_blocks);
    printf("Structure:      %06o\n", home.vlev);
    printf("Volume label:   %s\n", home.vnam[0] != '\0' ? home.vnam : "(none)");
    printf("Index bitmap:   %u block(s), starts at LBN %u\n",
           (unsigned)home.ibsz, (unsigned)home.iblb);
    printf("Max files:      %u\n", (unsigned)home.fmax);
    printf("Cluster factor: %u\n", (unsigned)home.sbcl);
    printf("Owner UIC:      %06o\n", home.vown);
    printf("Volume prot:    %06o\n", home.vpro);
    printf("Volume char:    %06o\n", home.vcha);
    printf("Default prot:   %06o\n", home.dfpr);
    printf("Window size:    %u\n", (unsigned)home.wisz);
    printf("File extend:    %u\n", (unsigned)home.fiex);
    printf("LRU count:      %u\n", (unsigned)home.lruc);
    if (home.revd[0] != '\0') {
        printf("Revision date:  %s\n", home.revd);
    }
    if (home.vdat[0] != '\0') {
        printf("Created:        %s\n", home.vdat);
    }

    rsx11_close_image(&img);
    return 0;
}

static int cmd_ls(const char *image_path,
                  const char *dir_filter, const char *spec_filter)
{
    rsx11_image_t img;
    rsx11_dirlist_t list;
    size_t i;
    size_t dir_width = 3u;
    size_t spec_width = 12u;
    size_t shown = 0;

    if (rsx11_open_image(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_read_directory(&img, &list) != 0) {
        perror("rsx11 directory");
        rsx11_close_image(&img);
        return 1;
    }

    for (i = 0; i < list.count; i++) {
        char spec[32];
        size_t dir_len;
        size_t spec_len;

        if (!match_dir(&list.entries[i], dir_filter) ||
            !match_spec(&list.entries[i], spec_filter)) {
            continue;
        }

        format_filespec(&list.entries[i], spec, sizeof(spec));
        dir_len = strlen(list.entries[i].dir);
        spec_len = strlen(spec);
        if (dir_len > dir_width) {
            dir_width = dir_len;
        }
        if (spec_len > spec_width) {
            spec_width = spec_len;
        }
    }

    printf("%-*s  %-*s  blocks  fid\n",
           (int)dir_width, "dir",
           (int)spec_width, "name.type;ver");
    for (i = 0; i < list.count; i++) {
        char spec[32];

        if (!match_dir(&list.entries[i], dir_filter) ||
            !match_spec(&list.entries[i], spec_filter)) {
            continue;
        }
        format_filespec(&list.entries[i], spec, sizeof(spec));
        printf("%-*s  %-*s  %6u  %06o,%06o,%06o\n",
               (int)dir_width, list.entries[i].dir,
               (int)spec_width, spec,
               (unsigned)list.entries[i].blocks,
               list.entries[i].fid.num,
               list.entries[i].fid.seq,
               list.entries[i].fid.rvn);
        shown++;
    }

    if (shown == 0u) {
        if (spec_filter[0] == '\0' && dir_filter[0] != '\0' &&
            find_empty_directory_marker(list.entries, list.count, dir_filter)) {
            rsx11_free_dirlist(&list);
            rsx11_close_image(&img);
            return 0;
        }
        rsx11_free_dirlist(&list);
        rsx11_close_image(&img);
        fprintf(stderr, "no matching directory entries\n");
        return 1;
    }
    rsx11_free_dirlist(&list);
    rsx11_close_image(&img);
    return 0;
}

static int cmd_extract(const char *image_path, const char *outdir,
                       const char *dir_filter, const char *spec_filter)
{
    rsx11_image_t img;
    rsx11_dirlist_t list;
    rsx11_dirent_t *selected = NULL;
    size_t selected_count = 0;
    size_t i;
    unsigned written = 0;

    if (rsx11_open_image(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_read_directory(&img, &list) != 0) {
        perror("rsx11 directory");
        rsx11_close_image(&img);
        return 1;
    }

    selected = calloc(list.count == 0u ? 1u : list.count, sizeof(*selected));
    if (selected == NULL) {
        perror("calloc");
        rsx11_free_dirlist(&list);
        rsx11_close_image(&img);
        return 1;
    }

    for (i = 0; i < list.count; i++) {
        if (!match_dir(&list.entries[i], dir_filter) ||
            !match_spec(&list.entries[i], spec_filter)) {
            continue;
        }
        selected[selected_count++] = list.entries[i];
    }

    if (selected_count == 0u) {
        fprintf(stderr, "no matching files\n");
        free(selected);
        rsx11_free_dirlist(&list);
        rsx11_close_image(&img);
        return 1;
    }

    if (rsx11_extract_selected(&img, selected, selected_count, outdir, 1,
                               &written) != 0) {
        perror("extract");
        free(selected);
        rsx11_free_dirlist(&list);
        rsx11_close_image(&img);
        return 1;
    }

    printf("extracted %u file(s)\n", written);

    free(selected);
    rsx11_free_dirlist(&list);
    rsx11_close_image(&img);
    return 0;
}

static int cmd_rm(const char *image_path,
                  const char *dir_filter, const char *spec_filter)
{
    rsx11_image_t img;
    rsx11_dirlist_t list;
    const rsx11_dirent_t *match = NULL;
    size_t i;
    char selector[64];

    if (rsx11_open_image_rw(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_read_directory(&img, &list) != 0) {
        perror("rsx11 directory");
        rsx11_close_image(&img);
        return 1;
    }

    for (i = 0; i < list.count; i++) {
        if (!match_dir(&list.entries[i], dir_filter) ||
            !match_spec(&list.entries[i], spec_filter)) {
            continue;
        }
        if (match != NULL) {
            fprintf(stderr, "ambiguous filespec, specify an exact version\n");
            rsx11_free_dirlist(&list);
            rsx11_close_image(&img);
            return 1;
        }
        match = &list.entries[i];
    }
    if (match == NULL) {
        fprintf(stderr, "no matching file\n");
        rsx11_free_dirlist(&list);
        rsx11_close_image(&img);
        return 1;
    }

    if (rsx11_remove_entry(&img, match) != 0) {
        perror("rm");
        rsx11_free_dirlist(&list);
        rsx11_close_image(&img);
        return 1;
    }

    format_selector(match, selector, sizeof(selector));
    printf("removed %s\n", selector);

    rsx11_free_dirlist(&list);
    rsx11_close_image(&img);
    return 0;
}

static int cmd_add(const char *image_path, const char *host_path,
                   const char *dir_filter, const char *spec_filter)
{
    rsx11_image_t img;
    rsx11_dirent_t ent;
    char name[10];
    char ext[4];
    int have_version;
    uint16_t version;
    char selector[64];

    if (parse_filespec(spec_filter, name, sizeof(name),
                       ext, sizeof(ext),
                       &have_version, &version) != 0) {
        perror("filespec");
        return 1;
    }

    if (rsx11_open_image_rw(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_add_file(&img, host_path, dir_filter, name, ext,
                       have_version, version, &ent) != 0) {
        perror("add");
        rsx11_close_image(&img);
        return 1;
    }

    format_selector(&ent, selector, sizeof(selector));
    printf("added %s\n", selector);

    rsx11_close_image(&img);
    return 0;
}

static int cmd_mkdir(const char *image_path, const char *dir_filter)
{
    rsx11_image_t img;

    if (rsx11_open_image_rw(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_make_directory(&img, dir_filter) != 0) {
        perror("mkdir");
        rsx11_close_image(&img);
        return 1;
    }

    printf("created %s\n", dir_filter);
    rsx11_close_image(&img);
    return 0;
}

static int cmd_rmdir(const char *image_path, const char *dir_filter)
{
    rsx11_image_t img;

    if (rsx11_open_image_rw(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_remove_directory(&img, dir_filter) != 0) {
        perror("rmdir");
        rsx11_close_image(&img);
        return 1;
    }

    printf("removed %s\n", dir_filter);
    rsx11_close_image(&img);
    return 0;
}

static int cmd_fsck(const char *image_path, int repair)
{
    rsx11_image_t img;
    rsx11_fsck_report_t report;
    int rc;

    if (repair) {
        if (rsx11_open_image_rw(&img, image_path) != 0) {
            perror(image_path);
            return 1;
        }
    } else {
        if (rsx11_open_image(&img, image_path) != 0) {
            perror(image_path);
            return 1;
        }
    }

    rc = rsx11_fsck(&img, repair, stdout, &report);
    rsx11_close_image(&img);
    if (rc != 0) {
        perror("fsck");
        return 1;
    }
    if (report.issues != 0u &&
        (!repair || report.fatal != 0u || report.issues != report.repaired)) {
        return 2;
    }
    return 0;
}

static int cmd_bootblock(int argc, char **argv)
{
    const char *image_path;
    const char *write_path = NULL;
    rsx11_image_t img;
    uint8_t block[RSX11_BLOCK_SIZE];
    uint32_t i;

    if (argc < 1) {
        usage(stderr);
        return 1;
    }
    image_path = argv[0];
    if (argc > 1) {
        int argi;

        for (argi = 1; argi < argc; argi++) {
            if (strcmp(argv[argi], "--write") == 0 && argi + 1 < argc) {
                write_path = argv[++argi];
                continue;
            }
            usage(stderr);
            return 1;
        }
    }

    if (rsx11_open_image(&img, image_path) != 0) {
        perror(image_path);
        return 1;
    }
    if (rsx11_read_boot_block(&img, block) != 0) {
        perror("bootblock");
        rsx11_close_image(&img);
        return 1;
    }
    rsx11_close_image(&img);

    if (write_path != NULL) {
        FILE *out = fopen(write_path, "wb");

        if (out == NULL) {
            perror(write_path);
            return 1;
        }
        if (fwrite(block, 1, sizeof(block), out) != sizeof(block) ||
            fclose(out) != 0) {
            perror("bootblock");
            return 1;
        }
        return 0;
    }

    for (i = 0; i < RSX11_BLOCK_SIZE; i += 16u) {
        uint32_t j;

        printf("%06o: ", (unsigned)i);
        for (j = 0; j < 16u; j++) {
            printf("%02x%s", block[i + j], j == 15u ? "" : " ");
        }
        printf("  ");
        for (j = 0; j < 16u; j++) {
            unsigned char c = block[i + j];

            putchar((c >= 32u && c < 127u) ? (int)c : '.');
        }
        putchar('\n');
    }
    return 0;
}

static int cmd_mkfs(int argc, char **argv)
{
    const char *image_path;
    const char *label = NULL;
    const char *boot_from = NULL;
    const char *bootblock = NULL;
    uint32_t blocks = 0;
    int blocks_set = 0;
    int type_set = 0;
    int i;

    if (argc < 1) {
        usage(stderr);
        return 1;
    }
    if (strcmp(argv[0], "--list") == 0) {
        rsx11_list_disk_types(stdout);
        return 0;
    }

    image_path = argv[0];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--blocks") == 0) {
            char *endptr;
            unsigned long value;

            if (i + 1 >= argc) {
                usage(stderr);
                return 1;
            }
            value = strtoul(argv[++i], &endptr, 0);
            if (*argv[i] == '\0' || *endptr != '\0' ||
                value == 0u || value > 0xffffffffu) {
                fprintf(stderr, "mkfs: invalid block count\n");
                return 1;
            }
            if (blocks_set || type_set) {
                fprintf(stderr,
                        "mkfs: use either --blocks or --type, not both\n");
                return 1;
            }
            blocks = (uint32_t)value;
            blocks_set = 1;
            continue;
        }
        if (strcmp(argv[i], "--type") == 0) {
            const rsx11_disk_type_t *type;

            if (i + 1 >= argc) {
                usage(stderr);
                return 1;
            }
            type = rsx11_find_disk_type(argv[++i]);
            if (type == NULL) {
                fprintf(stderr, "mkfs: unknown type '%s'\n", argv[i]);
                fprintf(stderr, "mkfs: use --list to see supported types\n");
                return 1;
            }
            if (blocks_set || type_set) {
                fprintf(stderr,
                        "mkfs: use either --blocks or --type, not both\n");
                return 1;
            }
            if ((type->bytes % RSX11_BLOCK_SIZE) != 0u) {
                fprintf(stderr, "mkfs: type size is not 512-byte aligned\n");
                return 1;
            }
            blocks = type->bytes / RSX11_BLOCK_SIZE;
            type_set = 1;
            continue;
        }
        if (strcmp(argv[i], "--label") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                return 1;
            }
            label = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--boot-from") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                return 1;
            }
            boot_from = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--bootblock") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                return 1;
            }
            bootblock = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--list") == 0) {
            rsx11_list_disk_types(stdout);
            return 0;
        }
        usage(stderr);
        return 1;
    }

    if (blocks == 0u) {
        fprintf(stderr, "mkfs requires --blocks N or --type <type>\n");
        return 1;
    }
    if (boot_from != NULL && bootblock != NULL) {
        fprintf(stderr, "mkfs: use only one of --boot-from and --bootblock\n");
        return 1;
    }
    if (rsx11_mkfs(image_path, blocks, label) != 0) {
        perror("mkfs");
        return 1;
    }
    if (boot_from != NULL) {
        if (install_bootblock_from_image(image_path, boot_from) != 0) {
            perror("mkfs bootstrap");
            return 1;
        }
    } else if (bootblock != NULL) {
        if (install_bootblock_from_file(image_path, bootblock) != 0) {
            perror("mkfs bootstrap");
            return 1;
        }
    }

    printf("created %s (%u blocks)\n", image_path, (unsigned)blocks);
    return 0;
}

int main(int argc, char **argv)
{
    char dir_filter[16];
    char spec_filter[32];

    if (argc < 3) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        if (argc != 3) {
            usage(stderr);
            return 1;
        }
        return cmd_info(argv[2]);
    }
    if (strcmp(argv[1], "ls") == 0) {
        if (argc > 4) {
            usage(stderr);
            return 1;
        }
        dir_filter[0] = '\0';
        spec_filter[0] = '\0';
        if (argc == 4 && parse_selector(argv[3], dir_filter, sizeof(dir_filter),
                                        spec_filter, sizeof(spec_filter)) != 0) {
            perror("selector");
            return 1;
        }
        return cmd_ls(argv[2], dir_filter, spec_filter);
    }
    if (strcmp(argv[1], "extract") == 0) {
        if (argc < 4 || argc > 5) {
            usage(stderr);
            return 1;
        }
        dir_filter[0] = '\0';
        spec_filter[0] = '\0';
        if (argc == 5 && parse_selector(argv[4], dir_filter, sizeof(dir_filter),
                                        spec_filter, sizeof(spec_filter)) != 0) {
            perror("selector");
            return 1;
        }
        return cmd_extract(argv[2], argv[3], dir_filter, spec_filter);
    }
    if (strcmp(argv[1], "rm") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 1;
        }
        if (parse_selector(argv[3], dir_filter, sizeof(dir_filter),
                           spec_filter, sizeof(spec_filter)) != 0) {
            perror("selector");
            return 1;
        }
        if (spec_filter[0] == '\0') {
            fprintf(stderr, "rm requires a file selector\n");
            return 1;
        }
        if (dir_filter[0] == '\0') {
            snprintf(dir_filter, sizeof(dir_filter), "MFD");
        }
        return cmd_rm(argv[2], dir_filter, spec_filter);
    }
    if (strcmp(argv[1], "add") == 0) {
        if (argc != 5) {
            usage(stderr);
            return 1;
        }
        if (parse_selector(argv[4], dir_filter, sizeof(dir_filter),
                           spec_filter, sizeof(spec_filter)) != 0) {
            perror("selector");
            return 1;
        }
        if (spec_filter[0] == '\0') {
            fprintf(stderr, "add requires a target filespec\n");
            return 1;
        }
        if (dir_filter[0] == '\0') {
            snprintf(dir_filter, sizeof(dir_filter), "MFD");
        }
        return cmd_add(argv[2], argv[3], dir_filter, spec_filter);
    }
    if (strcmp(argv[1], "mkdir") == 0 || strcmp(argv[1], "rmdir") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 1;
        }
        if (parse_selector(argv[3], dir_filter, sizeof(dir_filter),
                           spec_filter, sizeof(spec_filter)) != 0) {
            perror("selector");
            return 1;
        }
        if (dir_filter[0] == '\0' || spec_filter[0] != '\0' ||
            strcmp(dir_filter, "MFD") == 0) {
            fprintf(stderr, "%s requires a [grp,user] directory selector\n",
                    argv[1]);
            return 1;
        }
        if (strcmp(argv[1], "mkdir") == 0) {
            return cmd_mkdir(argv[2], dir_filter);
        }
        return cmd_rmdir(argv[2], dir_filter);
    }
    if (strcmp(argv[1], "fsck") == 0) {
        if (argc != 3 && argc != 4) {
            usage(stderr);
            return 1;
        }
        if (argc == 4 && strcmp(argv[3], "--repair") != 0) {
            usage(stderr);
            return 1;
        }
        return cmd_fsck(argv[2], argc == 4);
    }
    if (strcmp(argv[1], "bootblock") == 0) {
        return cmd_bootblock(argc - 2, &argv[2]);
    }
    if (strcmp(argv[1], "mkfs") == 0) {
        if (argc < 3) {
            usage(stderr);
            return 1;
        }
        return cmd_mkfs(argc - 2, &argv[2]);
    }

    usage(stderr);
    return 1;
}
