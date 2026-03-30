#ifndef LSI11_FILEIO_COMPAT_H
#define LSI11_FILEIO_COMPAT_H

#include <stdio.h>
#include <sys/types.h>

static inline int lsi11_fseeko(FILE *fp, off_t off, int whence)
{
#if defined(__sgi)
    return fseek(fp, (long)off, whence);
#else
    return fseeko(fp, off, whence);
#endif
}

static inline off_t lsi11_ftello(FILE *fp)
{
#if defined(__sgi)
    return (off_t)ftell(fp);
#else
    return ftello(fp);
#endif
}

#endif
