#include "hg_directory.h"

#include "rt11fs.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int hg_join_path(char *out, size_t out_size, const char *directory,
                        const char *name)
{
	int length = snprintf(out, out_size, "%s/%s", directory, name);

	if (length < 0 || (size_t)length >= out_size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

int hg_directory_prepare(const char *directory, const char *image_path,
                         uint32_t blocks)
{
	rt11_mkfs_opts_t options = {
		.volid = "HOSTDIR",
		.owner = "HG HOST DISK",
		.sysid = "DECRT11A"
	};
	rt11_image_t image;
	rt11_dirlist_t list;
	struct stat image_st;
	DIR *dir;
	struct dirent *entry;

	if (blocks < 128u || blocks > 65535u) {
		errno = EINVAL;
		return -1;
	}
	/* The image may contain acknowledged guest writes that were not exported
	 * before a crash. Never replace it with the older host-directory contents.
	 * Invalid existing images also remain untouched for recovery. */
	if (stat(image_path, &image_st) == 0) {
		int result;
		if (!S_ISREG(image_st.st_mode) || image_st.st_size <= 0 ||
		                image_st.st_size % RT11_BLOCK_SIZE != 0) {
			errno = EINVAL;
			return -1;
		}
		if (rt11_open_image(&image, image_path, "rb") != 0) {
			return -1;
		}
		result = rt11_read_directory(&image, &list);
		if (result == 0) {
			rt11_free_dirlist(&list);
		}
		rt11_close_image(&image);
		return result;
	}
	if (errno != ENOENT) {
		return -1;
	}
	/* Import host files only when creating a new mirror. */
	if (rt11_mkfs(image_path, blocks, &options, NULL) != 0) {
		return -1;
	}
	if (rt11_open_image(&image, image_path, "r+b") != 0) {
		return -1;
	}
	dir = opendir(directory);
	if (!dir) {
		rt11_close_image(&image);
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		rt11_name_t name;
		struct stat entry_st;

		if (entry->d_name[0] == '.' ||
		                rt11_name_from_host(entry->d_name, &name) != 0 ||
		                hg_join_path(path, sizeof(path), directory, entry->d_name) != 0 ||
		                stat(path, &entry_st) != 0 || !S_ISREG(entry_st.st_mode)) {
			continue;
		}
		if (rt11_add_file(&image, path, &name) != 0 && errno != EEXIST)
			fprintf(stderr, "hgfsd: cannot import %s: %s\n", path,
			        strerror(errno));
	}
	closedir(dir);
	rt11_close_image(&image);
	return 0;
}

int hg_directory_export(const char *directory, const char *image_path)
{
	rt11_image_t image;
	rt11_dirlist_t list;
	size_t i;
	int result = 0;

	if (rt11_open_image(&image, image_path, "rb") != 0) {
		return -1;
	}
	if (rt11_read_directory(&image, &list) != 0) {
		rt11_close_image(&image);
		return -1;
	}

	for (i = 0; i < list.count; i++) {
		const rt11_dirent_t *entry = &list.entries[i];
		char name[16];
		char target[PATH_MAX];
		char temporary[PATH_MAX];
		int temporary_length;

		if ((entry->status & RT11_E_PERM) == 0) {
			continue;
		}
		if (entry->ext[0]) {
			snprintf(name, sizeof(name), "%s.%s", entry->name, entry->ext);
		} else {
			snprintf(name, sizeof(name), "%s", entry->name);
		}
		temporary_length = snprintf(temporary, sizeof(temporary),
		                            "%s/.hgfs.%s.tmp", directory, name);
		if (hg_join_path(target, sizeof(target), directory, name) != 0 ||
		                temporary_length < 0 ||
		                (size_t)temporary_length >= sizeof(temporary)) {
			result = -1;
			continue;
		}
		if (rt11_extract_file(&image, entry, temporary) != 0 ||
		                rename(temporary, target) != 0) {
			fprintf(stderr, "hgfsd: cannot export %s: %s\n", target,
			        strerror(errno));
			unlink(temporary);
			result = -1;
		}
	}

	rt11_free_dirlist(&list);
	rt11_close_image(&image);
	return result;
}
