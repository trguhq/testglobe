/*
 * testglobe - private file-publication helpers for native build tools.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Included by e_bin.c and tga_pack.c only, never by the application. Static
 * C89 helpers keep each tool a single-source build on historical compilers.
 * Define BUILD_PATH_MAX before inclusion; feature-test macros must precede
 * all system headers in the including source.
 */
#ifndef buildio_h
#define buildio_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <io.h>
typedef struct _stat Build_stat;
#define build_stat _stat
#define build_lstat _stat
#define build_regular(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define build_open(path) _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE)
#define build_fdopen _fdopen
#define build_close _close
#else
#include <unistd.h>
typedef struct stat Build_stat;
#define build_stat stat
#define build_lstat lstat
#define build_regular(mode) S_ISREG(mode)
#define build_open(path) open(path, O_CREAT | O_EXCL | O_WRONLY, 0666)
#define build_fdopen fdopen
#define build_close close
#endif

/* A FIFO must not be opened to test existence. lstat also reserves dangling
 * symlinks rather than treating their nonexistent destinations as free names. */
static int build_entry(const char *path, Build_stat *info)
{
    if (build_lstat(path, info) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}

static int build_same_file(const char *first, const Build_stat *first_info,
                            const char *second, const Build_stat *second_info)
{
#ifdef _MSC_VER
    static char first_path[BUILD_PATH_MAX], second_path[BUILD_PATH_MAX];
    (void)first_info;
    (void)second_info;
    /* DOS inode fields are not identities. Resolve drive-relative paths and
     * dot components, then compare case-insensitive absolute filenames. */
    if (_fullpath(first_path, first, sizeof(first_path)) == NULL ||
        _fullpath(second_path, second, sizeof(second_path)) == NULL) return -1;
    return _stricmp(first_path, second_path) == 0;
#else
    (void)first;
    (void)second;
    return first_info->st_dev == second_info->st_dev &&
           first_info->st_ino == second_info->st_ino;
#endif
}

/* Replace the extension rather than appending one, retaining DOS 8.3 names.
 * Each caller supplies its actual destination capacity. */
static int build_sibling(char *path, size_t capacity, const char *output,
                           const char *extension)
{
    const char *cursor, *dot;
    size_t length, suffix;
    dot = NULL;
    for (cursor = output; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':') dot = NULL;
        else if (*cursor == '.') dot = cursor;
    }
    length = dot == NULL ? strlen(output) : (size_t)(dot - output);
    suffix = strlen(extension);
    if (length >= capacity || suffix >= capacity - length) return 0;
    memcpy(path, output, length);
    strcpy(path + length, extension);
#ifdef _MSC_VER
    return _stricmp(path, output) != 0;
#else
    return strcmp(path, output) != 0;
#endif
}

/* Reserve a new name without following links or truncating an existing file. */
static FILE *build_create(const char *path)
{
    int descriptor;
    FILE *stream;
    descriptor = build_open(path);
    if (descriptor < 0) return NULL;
    stream = build_fdopen(descriptor, "wb");
    if (stream == NULL)
    {
        build_close(descriptor);
        remove(path);
    }
    return stream;
}

/* DOS rename refuses an existing destination. On POSIX, publish a hard link
 * first so neither installation nor rollback overwrites a newly created file.
 * Source and destination are siblings on the same filesystem. */
static int build_move_new(const char *source, const char *destination)
{
#ifdef _MSC_VER
    return rename(source, destination) == 0;
#else
    if (link(source, destination) != 0) return 0;
    if (remove(source) != 0)
    {
        remove(destination);
        return 0;
    }
    return 1;
#endif
}

#endif /* buildio_h */
