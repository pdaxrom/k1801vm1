#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef enum {
    IMG_DISK = 0,
    IMG_TAPE = 1
} img_kind_t;

typedef struct {
    const char *name;
    img_kind_t kind;
    uint64_t default_bytes;
    uint32_t sector_bytes;
    const char *desc;
} img_type_t;

#define KB (1024ull)
#define MB (1024ull * KB)
#define GB (1024ull * MB)

/* RK05 (RK11) */
#define RK05_CYLINDERS 203u
#define RK05_HEADS 2u
#define RK05_SECTORS 12u
#define RK05_SECTOR_BYTES 512u
#define RK05_BYTES                                                    \
    ((uint64_t)RK05_CYLINDERS * RK05_HEADS * RK05_SECTORS * RK05_SECTOR_BYTES)

/* RK06/RK07 (RH11) */
#define RH11_HEADS 3u
#define RH11_SECTORS 22u
#define RH11_SECTOR_BYTES 512u
#define RK06_CYLINDERS 411u
#define RK07_CYLINDERS 815u
#define RK06_BYTES                                                    \
    ((uint64_t)RK06_CYLINDERS * RH11_HEADS * RH11_SECTORS * RH11_SECTOR_BYTES)
#define RK07_BYTES                                                    \
    ((uint64_t)RK07_CYLINDERS * RH11_HEADS * RH11_SECTORS * RH11_SECTOR_BYTES)

/* RL01/RL02 (RL11) */
#define RL_HEADS 2u
#define RL_SECTORS 40u
#define RL_SECTOR_BYTES 256u
#define RL01_CYLINDERS 256u
#define RL02_CYLINDERS 512u
#define RL01_BYTES                                                    \
    ((uint64_t)RL01_CYLINDERS * RL_HEADS * RL_SECTORS * RL_SECTOR_BYTES)
#define RL02_BYTES                                                    \
    ((uint64_t)RL02_CYLINDERS * RL_HEADS * RL_SECTORS * RL_SECTOR_BYTES)

/* RM05 (XP/RP) */
#define RM05_SECTORS_PER_SURF 32u
#define RM05_SURFACES 19u
#define RM05_CYLINDERS 823u
#define RM05_SECTOR_BYTES 512u
#define RM05_BYTES                                                    \
    ((uint64_t)RM05_SECTORS_PER_SURF * RM05_SURFACES * RM05_CYLINDERS * \
     RM05_SECTOR_BYTES)

/* MSCP (RQ11) */
#define MSCP_SECTOR_BYTES 512u

static const img_type_t img_types[] = {
    {"rk05", IMG_DISK, RK05_BYTES, RK05_SECTOR_BYTES, "RK11 RK05"},
    {"rk06", IMG_DISK, RK06_BYTES, RH11_SECTOR_BYTES, "RH11 RK06"},
    {"rk07", IMG_DISK, RK07_BYTES, RH11_SECTOR_BYTES, "RH11 RK07"},
    {"rl01", IMG_DISK, RL01_BYTES, RL_SECTOR_BYTES, "RL11 RL01"},
    {"rl02", IMG_DISK, RL02_BYTES, RL_SECTOR_BYTES, "RL11 RL02"},
    {"rm05", IMG_DISK, RM05_BYTES, RM05_SECTOR_BYTES, "XP/RP RM05"},
    {"rd31", IMG_DISK, 41560ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RD31"},
    {"rd32", IMG_DISK, 83204ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RD32"},
    {"rd51", IMG_DISK, 21600ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RD51"},
    {"rd52", IMG_DISK, 60480ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RD52"},
    {"rd53", IMG_DISK, 138672ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RD53"},
    {"rd54", IMG_DISK, 311200ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RD54"},
    {"ra60", IMG_DISK, 400176ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RA60"},
    {"ra70", IMG_DISK, 547041ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RA70"},
    {"ra71", IMG_DISK, 1367310ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES,
     "RQ RA71"},
    {"ra80", IMG_DISK, 237212ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RA80"},
    {"ra81", IMG_DISK, 891072ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RA81"},
    {"ra82", IMG_DISK, 1216665ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES,
     "RQ RA82"},
    {"rx50", IMG_DISK, 800ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RX50"},
    {"rx33", IMG_DISK, 2400ull * MSCP_SECTOR_BYTES, MSCP_SECTOR_BYTES, "RQ RX33"},
    {"tk50", IMG_TAPE, 0, 0, "TQ11/TMSCP TK50 (.tap)"},
    {NULL, 0, 0, 0, NULL}
};

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: lsi11-image -t <type> -o <path> [--size <bytes>] [--force] "
            "[--list]\n"
            "Options:\n"
            "  -t, --type <name>   Image type (use --list to see all)\n"
            "  -o, --output <path> Output file path\n"
            "  -s, --size <bytes>  Override size (suffix K/M/G allowed)\n"
            "  -f, --force         Overwrite existing file\n"
            "  -l, --list          List supported types\n"
            "  -h, --help          Show this help\n"
            "\n"
            "Notes:\n"
            "  - Sizes are in bytes unless a K/M/G suffix is provided.\n"
            "  - Disk sizes must align to the device sector size.\n"
            "  - TK50 (.tap) images are created empty; size overrides are not "
            "supported.\n");
}

static const img_type_t *find_type(const char *name)
{
    const img_type_t *t;

    if (!name) {
        return NULL;
    }
    for (t = img_types; t->name; t++) {
        if (strcasecmp(name, t->name) == 0) {
            return t;
        }
    }
    return NULL;
}

static int parse_size(const char *s, uint64_t *out)
{
    char *end = NULL;
    uint64_t mul = 1;
    unsigned long long base;

    if (!s || !*s || !out) {
        return -1;
    }

    errno = 0;
    base = strtoull(s, &end, 0);
    if (errno != 0 || end == s) {
        return -1;
    }

    if (*end != '\0') {
        char c = (char)tolower((unsigned char)*end);
        const char *rest = end + 1;

        if ((c == 'k' || c == 'm' || c == 'g') &&
                (*rest == 'b' || *rest == 'B')) {
            rest++;
        }
        if (*rest != '\0') {
            return -1;
        }
        switch (c) {
        case 'k':
            mul = KB;
            break;
        case 'm':
            mul = MB;
            break;
        case 'g':
            mul = GB;
            break;
        case 'b':
            mul = 1;
            break;
        default:
            return -1;
        }
    }

    if (base > (unsigned long long)(UINT64_MAX / mul)) {
        return -1;
    }
    *out = (uint64_t)base * mul;
    return 0;
}

static void list_types(FILE *out)
{
    const img_type_t *t;

    fprintf(out, "Supported types:\n");
    for (t = img_types; t->name; t++) {
        if (t->kind == IMG_TAPE) {
            fprintf(out, "  %-5s tape  empty .tap (%s)\n", t->name, t->desc);
            continue;
        }
        fprintf(out, "  %-5s disk  %llu bytes (%llu blocks of %u)  %s\n",
                t->name,
                (unsigned long long)t->default_bytes,
                (unsigned long long)(t->default_bytes / t->sector_bytes),
                t->sector_bytes,
                t->desc);
    }
}

static int create_image(const char *path, uint64_t size, int force)
{
    int fd;
    int flags = O_CREAT | O_WRONLY;

    if (!path || !*path) {
        errno = EINVAL;
        return -1;
    }
    flags |= force ? O_TRUNC : O_EXCL;
    fd = open(path, flags, 0644);
    if (fd < 0) {
        return -1;
    }
    if (size > 0) {
        if (size > (uint64_t)LLONG_MAX) {
            close(fd);
            errno = EFBIG;
            return -1;
        }
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            return -1;
        }
    }
    if (close(fd) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *type_name = NULL;
    const char *path = NULL;
    const char *size_arg = NULL;
    const img_type_t *type = NULL;
    uint64_t size = 0;
    int force = 0;
    int list = 0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-t") == 0 || strcmp(arg, "--type") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing type after %s\n", arg);
                usage(stderr);
                return 1;
            }
            type_name = argv[++i];
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing path after %s\n", arg);
                usage(stderr);
                return 1;
            }
            path = argv[++i];
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--size") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing size after %s\n", arg);
                usage(stderr);
                return 1;
            }
            size_arg = argv[++i];
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
            force = 1;
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list") == 0) {
            list = 1;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            usage(stderr);
            return 1;
        }
    }

    if (list) {
        list_types(stdout);
        return 0;
    }

    if (!type_name || !path) {
        usage(stderr);
        return 1;
    }

    type = find_type(type_name);
    if (!type) {
        fprintf(stderr, "Unknown type: %s\n", type_name);
        list_types(stderr);
        return 1;
    }

    if (size_arg) {
        if (parse_size(size_arg, &size) != 0) {
            fprintf(stderr, "Invalid size: %s\n", size_arg);
            return 1;
        }
    } else {
        size = type->default_bytes;
    }

    if (type->kind == IMG_TAPE) {
        if (size_arg && size != 0) {
            fprintf(stderr, "Size override is not supported for tape images\n");
            return 1;
        }
        size = 0;
    } else {
        if (size == 0) {
            fprintf(stderr, "Disk size must be non-zero\n");
            return 1;
        }
        if (type->sector_bytes && (size % type->sector_bytes) != 0) {
            fprintf(stderr,
                    "Size must be a multiple of %u bytes for type %s\n",
                    type->sector_bytes, type->name);
            return 1;
        }
    }

    if (create_image(path, size, force) != 0) {
        fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
        return 1;
    }

    return 0;
}
