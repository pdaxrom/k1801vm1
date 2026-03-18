#include "rsx11fs.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: rsx11tool <command> [args]\n"
            "Commands:\n"
            "  info <image>\n"
            "  ls <image> [MFD|[grp,user]]\n"
            "  extract <image> <outdir> [[grp,user]][NAME.EXT[;ver]]\n"
            "  add <image> <hostfile> [[grp,user]]NAME.EXT[;ver]\n"
            "  rm <image> [[grp,user]]NAME.EXT[;ver]\n"
            "  mkdir <image> [grp,user]\n"
            "  rmdir <image> [grp,user]\n");
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

static int cmd_ls(const char *image_path, const char *dir_filter)
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

        if (!match_dir(&list.entries[i], dir_filter)) {
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

        if (!match_dir(&list.entries[i], dir_filter)) {
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
        if (dir_filter[0] != '\0' &&
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
        if (spec_filter[0] != '\0') {
            fprintf(stderr, "ls accepts only directory selectors\n");
            return 1;
        }
        return cmd_ls(argv[2], dir_filter);
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

    usage(stderr);
    return 1;
}
