#ifndef RSX11FS_H
#define RSX11FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RSX11_BLOCK_SIZE 512u

typedef struct {
    uint16_t num;
    uint16_t seq;
    uint16_t rvn;
} rsx11_fid_t;

typedef struct {
    FILE *fp;
    uint64_t size_bytes;
    uint32_t total_blocks;
} rsx11_image_t;

typedef struct {
    uint16_t ibsz;
    uint32_t iblb;
    uint16_t fmax;
    uint16_t sbcl;
    uint16_t dvty;
    uint16_t vlev;
    char vnam[13];
    uint16_t vown;
    uint16_t vpro;
    uint16_t vcha;
    uint16_t dfpr;
    uint8_t wisz;
    uint8_t fiex;
    uint8_t lruc;
    char revd[8];
    char vdat[15];
} rsx11_home_t;

typedef struct {
    char dir[16];
    char name[10];
    char ext[4];
    uint16_t version;
    rsx11_fid_t fid;
    uint32_t blocks;
    uint64_t raw_offset;
} rsx11_dirent_t;

typedef struct {
    rsx11_dirent_t *entries;
    size_t count;
} rsx11_dirlist_t;

typedef struct {
    size_t checked;
    size_t issues;
    size_t repaired;
    size_t fatal;
} rsx11_fsck_report_t;

int rsx11_open_image(rsx11_image_t *img, const char *path);
int rsx11_open_image_rw(rsx11_image_t *img, const char *path);
void rsx11_close_image(rsx11_image_t *img);

int rsx11_read_boot_block(rsx11_image_t *img,
                          uint8_t block[RSX11_BLOCK_SIZE]);
int rsx11_write_boot_block(rsx11_image_t *img,
                           const uint8_t block[RSX11_BLOCK_SIZE]);
int rsx11_read_home(rsx11_image_t *img, rsx11_home_t *home);

int rsx11_read_directory(rsx11_image_t *img, rsx11_dirlist_t *out);
int rsx11_extract_selected(rsx11_image_t *img,
                           const rsx11_dirent_t *entries, size_t count,
                           const char *outdir, int preserve_dirs,
                           unsigned *files_out);
int rsx11_remove_entry(rsx11_image_t *img, const rsx11_dirent_t *ent);
int rsx11_make_directory(rsx11_image_t *img, const char *dir);
int rsx11_remove_directory(rsx11_image_t *img, const char *dir);
int rsx11_add_file(rsx11_image_t *img, const char *host_path,
                   const char *dir, const char *name, const char *ext,
                   int have_version, uint16_t version,
                   rsx11_dirent_t *out);
int rsx11_mkfs(const char *path, uint32_t blocks, const char *label);
int rsx11_fsck(rsx11_image_t *img, int repair, FILE *out,
               rsx11_fsck_report_t *report);
void rsx11_free_dirlist(rsx11_dirlist_t *list);

#endif
