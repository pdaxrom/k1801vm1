#ifndef RT11FS_H
#define RT11FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RT11_BLOCK_SIZE 512u

typedef struct {
    FILE *fp;
    uint32_t total_blocks;
    uint32_t base_block;
    uint32_t partition_blocks;
    uint32_t volume_blocks;
    uint32_t partition_number;
} rt11_image_t;

typedef struct {
    uint16_t status;
    char name[7];
    char ext[4];
    uint16_t length;
    uint16_t job;
    uint16_t date;
    uint32_t start_block;
    uint32_t raw_offset;
    uint16_t segment;
    uint16_t entry_index;
} rt11_dirent_t;

typedef struct {
    rt11_dirent_t *entries;
    size_t count;
} rt11_dirlist_t;

typedef struct {
    uint16_t name_words[2];
    uint16_t ext_word;
} rt11_name_t;

int rt11_open_image(rt11_image_t *img, const char *path, const char *mode);
void rt11_close_image(rt11_image_t *img);
int rt11_set_partition(rt11_image_t *img, uint32_t partition);

int rt11_read_home(rt11_image_t *img, uint16_t *dir_start_out);

int rt11_read_directory(rt11_image_t *img, rt11_dirlist_t *out);
void rt11_free_dirlist(rt11_dirlist_t *list);

int rt11_name_from_host(const char *host, rt11_name_t *out);
void rt11_name_to_host(const rt11_name_t *name, char *out, size_t out_size,
                        int lower);

int rt11_find_file(rt11_image_t *img, const rt11_name_t *name,
                   rt11_dirent_t *ent_out);
int rt11_extract_file(rt11_image_t *img, const rt11_dirent_t *ent,
                      const char *out_path);
int rt11_add_file(rt11_image_t *img, const char *host_path,
                  const rt11_name_t *name);
int rt11_remove_file(rt11_image_t *img, const rt11_name_t *name);

int rt11_mkfs(const char *path, uint32_t total_blocks,
              uint32_t *usable_blocks_out);

#endif
