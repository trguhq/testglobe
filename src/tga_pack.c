/*
 * testglobe - append a TIGA module to a DOS executable without loading it.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: tga_pack input.exe output.exe [module.rlm]
 * Without a module, publish the unmodified software-transform executable.
 * The output's .tmp and .bak siblings are reserved during publication.
 */
#if !defined(_MSC_VER) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 1
#endif
#if !defined(_MSC_VER) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 500
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "tga_cmd.h"

#ifdef _MSC_VER
#include <io.h>
#define Tga_stat struct _stat
#define tga_stat _stat
#define tga_lstat _stat
#define tga_regular(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define tga_open(path) _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE)
#define tga_fdopen _fdopen
#define tga_close _close
#else
#include <unistd.h>
#define Tga_stat struct stat
#define tga_stat stat
#define tga_lstat lstat
#define tga_regular(mode) S_ISREG(mode)
#define tga_open(path) open(path, O_CREAT | O_EXCL | O_WRONLY, 0666)
#define tga_fdopen fdopen
#define tga_close close
#endif

#define TGA_PACK_PATH 260U

static int tga_entry(const char *path, Tga_stat *info)
{
    if (tga_lstat(path, info) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}

static int tga_same(const char *first, const Tga_stat *a,
                    const char *second, const Tga_stat *b)
{
#ifdef _MSC_VER
    static char first_path[TGA_PACK_PATH], second_path[TGA_PACK_PATH];
    (void)a; (void)b;
    if (_fullpath(first_path, first, sizeof(first_path)) == NULL ||
        _fullpath(second_path, second, sizeof(second_path)) == NULL) return -1;
    return _stricmp(first_path, second_path) == 0;
#else
    (void)first; (void)second;
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
#endif
}

static int tga_sibling(char *result, const char *output, const char *extension)
{
    const char *cursor, *dot = NULL;
    size_t length;
    for (cursor = output; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':') dot = NULL;
        else if (*cursor == '.') dot = cursor;
    }
    length = dot == NULL ? strlen(output) : (size_t)(dot - output);
    if (length >= TGA_PACK_PATH || strlen(extension) + 1U > TGA_PACK_PATH - length)
        return 0;
    memcpy(result, output, length);
    strcpy(result + length, extension);
#ifdef _MSC_VER
    return _stricmp(result, output) != 0;
#else
    return strcmp(result, output) != 0;
#endif
}

static FILE *tga_create(const char *path)
{
    FILE *stream;
    int descriptor = tga_open(path);
    if (descriptor < 0) return NULL;
    stream = tga_fdopen(descriptor, "wb");
    if (stream == NULL)
    {
        tga_close(descriptor);
        remove(path);
    }
    return stream;
}

/* Match DOS rename's no-replacement semantics on the host as well. */
static int tga_move_new(const char *source, const char *destination)
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

static unsigned long tga_u16(const unsigned char *bytes)
{
    return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8);
}

static void tga_put32(unsigned char *bytes, unsigned long value)
{
    unsigned int i;
    for (i = 0; i < 4U; ++i)
    {
        bytes[i] = (unsigned char)(value & 255UL);
        value >>= 8;
    }
}

static int tga_executable(FILE *input, unsigned long size)
{
    unsigned char header[28], trailer[TGA_RLM_TRAILER_SIZE];
    unsigned long last, pages, image, header_size;
    if (size < sizeof(header) || fread(header, 1, sizeof(header), input) != sizeof(header))
        return 0;
    if (header[0] != 'M' || header[1] != 'Z') return 0;
    last = tga_u16(header + 2);
    pages = tga_u16(header + 4);
    header_size = tga_u16(header + 8) * 16UL;
    if (pages == 0UL || last > 511UL) return 0;
    image = (pages - 1UL) * 512UL + (last != 0UL ? last : 512UL);
    if (header_size < sizeof(header) || header_size > image || image > size) return 0;
    /* An already packed input would retain an obsolete embedded payload.
     * Build from the linked host EXE instead; repeated builds stay bounded. */
    if (size >= TGA_RLM_TRAILER_SIZE)
    {
        if (fseek(input, (long)(size - TGA_RLM_TRAILER_SIZE), SEEK_SET) != 0 ||
            fread(trailer, 1, sizeof(trailer), input) != sizeof(trailer)) return 0;
        if (memcmp(trailer, TGA_RLM_MAGIC, 8U) == 0) return 0;
    }
    return fseek(input, 0L, SEEK_SET) == 0;
}

static int tga_copy(FILE *input, FILE *output, unsigned long size,
                     unsigned long *checksum)
{
    unsigned char bytes[512];
    unsigned int n, i, bit;
    unsigned long crc = 0xffffffffUL;
    while (size != 0UL)
    {
        n = size > sizeof(bytes) ? sizeof(bytes) : (unsigned int)size;
        if (fread(bytes, 1, n, input) != n || fwrite(bytes, 1, n, output) != n)
            return 0;
        if (checksum != NULL)
            for (i = 0; i < n; ++i)
            {
                crc ^= bytes[i];
                for (bit = 0; bit < 8U; ++bit)
                    crc = (crc >> 1) ^ ((crc & 1UL) ? 0xedb88320UL : 0UL);
            }
        size -= n;
    }
    if (fgetc(input) != EOF || ferror(input)) return 0;
    if (checksum != NULL) *checksum = crc ^ 0xffffffffUL;
    return 1;
}

static int tga_output(const char *path, Tga_stat *info,
                       int inputs, char **names, const Tga_stat *source)
{
    int i, same, exists = tga_entry(path, info);
    if (exists < 0 || (exists && !tga_regular(info->st_mode))) return -1;
    if (!exists) return 0;
    for (i = 0; i < inputs; ++i)
    {
        same = tga_same(path, info, names[i], &source[i]);
        if (same != 0) return -1;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static char temporary[TGA_PACK_PATH], backup[TGA_PACK_PATH];
    unsigned char trailer[TGA_RLM_TRAILER_SIZE];
    char *names[2];
    FILE *input[2], *output;
    Tga_stat source[2], initial, current, sibling;
    unsigned long sizes[2], checksum = 0UL;
    int i, inputs, valid, had_output, has_output;
    if (argc != 3 && argc != 4)
    {
        fputs("Usage: tga_pack input.exe output.exe [module.rlm]\n", stderr);
        return EXIT_FAILURE;
    }
    inputs = argc - 2;
    names[0] = argv[1]; names[1] = argc == 4 ? argv[3] : NULL;
    input[0] = input[1] = NULL;
    output = NULL;
    valid = 0;
    if (!tga_sibling(temporary, argv[2], ".tmp") ||
        !tga_sibling(backup, argv[2], ".bak") ||
        tga_entry(temporary, &sibling) != 0 || tga_entry(backup, &sibling) != 0)
    {
        fputs("tga_pack: output requires unused .tmp and .bak siblings\n", stderr);
        goto done;
    }
    for (i = 0; i < inputs; ++i)
    {
        if (tga_stat(names[i], &source[i]) != 0 || !tga_regular(source[i].st_mode) ||
            source[i].st_size <= 0 || source[i].st_size > 0x7fffffffL)
        {
            fputs("tga_pack: input must be a nonempty regular file below 2 GiB\n", stderr);
            goto done;
        }
        sizes[i] = (unsigned long)source[i].st_size;
        input[i] = fopen(names[i], "rb");
        if (input[i] == NULL)
        {
            fputs("tga_pack: cannot open input\n", stderr);
            goto done;
        }
    }
    if (!tga_executable(input[0], sizes[0]))
    {
        fputs("tga_pack: input must be an unpacked DOS MZ executable\n", stderr);
        goto done;
    }
    if (inputs == 2 && (sizes[1] > TGA_RLM_MAX_SIZE ||
        sizes[0] > 0x7fffffffUL - TGA_RLM_TRAILER_SIZE - sizes[1]))
    {
        fputs("tga_pack: module or combined executable is too large\n", stderr);
        goto done;
    }
    had_output = tga_output(argv[2], &initial, inputs, names, source);
    if (had_output < 0)
    {
        fputs("tga_pack: output must be a separate regular file\n", stderr);
        goto done;
    }
    output = tga_create(temporary);
    if (output == NULL)
    {
        fputs("tga_pack: cannot reserve temporary output\n", stderr);
        goto done;
    }
    valid = tga_copy(input[0], output, sizes[0], NULL);
    if (valid && inputs == 2)
    {
        valid = tga_copy(input[1], output, sizes[1], &checksum);
        memset(trailer, 0, sizeof(trailer));
        memcpy(trailer, TGA_RLM_MAGIC, 8U);
        tga_put32(trailer + 8, sizes[0]);
        tga_put32(trailer + 12, sizes[1]);
        tga_put32(trailer + 16, checksum);
        if (valid && fwrite(trailer, 1, sizeof(trailer), output) != sizeof(trailer)) valid = 0;
    }
    if (fclose(output) != 0) valid = 0;
    output = NULL;
    for (i = 0; i < inputs; ++i)
    {
        if (fclose(input[i]) != 0) valid = 0;
        input[i] = NULL;
    }
    if (!valid)
    {
        remove(temporary);
        fputs("tga_pack: copy failed; previous output preserved\n", stderr);
        goto done;
    }
    valid = 0;
    has_output = tga_output(argv[2], &current, inputs, names, source);
    if (has_output < 0 || has_output != had_output ||
        (had_output && (tga_same(argv[2], &initial, argv[2], &current) != 1 ||
         initial.st_size != current.st_size || initial.st_mtime != current.st_mtime)))
    {
        remove(temporary);
        fputs("tga_pack: output changed while packing; not replaced\n", stderr);
        goto done;
    }
    if (had_output && !tga_move_new(argv[2], backup))
    {
        remove(temporary);
        fputs("tga_pack: cannot preserve previous output\n", stderr);
        goto done;
    }
    if (!tga_move_new(temporary, argv[2]))
    {
        if (had_output && !tga_move_new(backup, argv[2]))
            fprintf(stderr, "tga_pack: previous output remains in %s\n", backup);
        remove(temporary);
        fputs("tga_pack: cannot install executable\n", stderr);
        goto done;
    }
    if (had_output && remove(backup) != 0)
    {
        fputs("tga_pack: cannot remove old executable backup\n", stderr);
        goto done;
    }
    valid = 1;
done:
    if (output != NULL) fclose(output);
    for (i = 0; i < 2; ++i) if (input[i] != NULL) fclose(input[i]);
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
