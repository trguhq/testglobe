/*
 * testglobe - embed canonical Earth map binaries as segmented C89 data.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build this host tool without application driver defines, then run:
 *     e_bin data_directory output_file
 * Every input is checked before an existing output is replaced. The .tmp
 * and .bak siblings are reserved during generation; their names stay 8.3.
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
#include "earthres.h"

#ifdef _MSC_VER
#include <io.h>
#define Earth_stat struct _stat
#define earth_stat _stat
#define earth_lstat _stat
#define earth_regular(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define earth_open(path) _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE)
#define earth_fdopen _fdopen
#define earth_close _close
#else
#include <unistd.h>
#define Earth_stat struct stat
#define earth_stat stat
#define earth_lstat lstat
#define earth_regular(mode) S_ISREG(mode)
#define earth_open(path) open(path, O_CREAT | O_EXCL | O_WRONLY, 0666)
#define earth_fdopen fdopen
#define earth_close close
#endif

#define EARTH_PATH_MAX 1024

struct Earth_chunk {
    const char *file;
    const char *name;
    int longitude;
    unsigned long size;
};

static const struct Earth_chunk earth_chunks[] = {
    { "e32.bin", "earth_data_32", 32, 512UL },
    { "e64.bin", "earth_data_64", 64, 2048UL },
    { "e128.bin", "earth_data_128", 128, 8192UL },
    { "e256_0.bin", "earth_data_256_0", 256, TG_EARTH_CHUNK_SIZE },
    { "e256_1.bin", "earth_data_256_1", 256, TG_EARTH_CHUNK_SIZE },
    { "e512_0.bin", "earth_data_512_0", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_1.bin", "earth_data_512_1", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_2.bin", "earth_data_512_2", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_3.bin", "earth_data_512_3", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_4.bin", "earth_data_512_4", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_5.bin", "earth_data_512_5", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_6.bin", "earth_data_512_6", 512, TG_EARTH_CHUNK_SIZE },
    { "e512_7.bin", "earth_data_512_7", 512, TG_EARTH_CHUNK_SIZE }
};

static int earth_path(char *path, const char *directory, const char *name)
{
    size_t length;
    length = strlen(directory);
    if (length >= EARTH_PATH_MAX ||
        strlen(name) + 2U > EARTH_PATH_MAX - length) return 0;
    strcpy(path, directory);
    if (length != 0 && directory[length - 1U] != '/' &&
        directory[length - 1U] != '\\' && directory[length - 1U] != ':')
        strcat(path, "/");
    strcat(path, name);
    return 1;
}

/* Replace the extension rather than appending one, for DOS 8.3 paths. */
static int earth_sibling(char *path, const char *output, const char *extension)
{
    const char *cursor, *dot;
    size_t length;
    dot = NULL;
    for (cursor = output; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':') dot = NULL;
        else if (*cursor == '.') dot = cursor;
    }
    length = dot == NULL ? strlen(output) : (size_t)(dot - output);
    if (length >= EARTH_PATH_MAX ||
        strlen(extension) + 1U > EARTH_PATH_MAX - length) return 0;
    memcpy(path, output, length);
    strcpy(path + length, extension);
#ifdef _MSC_VER
    return _stricmp(path, output) != 0;
#else
    return strcmp(path, output) != 0;
#endif
}

/* Do not open paths merely to test existence: FIFOs can block, and a dangling
 * symlink must not be mistaken for an unused temporary or backup name. */
static int earth_entry(const char *path, Earth_stat *info)
{
    if (earth_lstat(path, info) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}

static int earth_same_file(const char *first, const Earth_stat *first_info,
                           const char *second, const Earth_stat *second_info)
{
#ifdef _MSC_VER
    static char first_path[EARTH_PATH_MAX], second_path[EARTH_PATH_MAX];
    (void)first_info;
    (void)second_info;
    /* DOS stat inode fields are not useful identifiers. _fullpath resolves
     * drive-relative paths and dot components; DOS filenames ignore case. */
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

static int earth_output(const char *directory, const char *path,
                         Earth_stat *info)
{
    static char input[EARTH_PATH_MAX];
    Earth_stat input_info;
    unsigned int i;
    int exists;
    exists = earth_entry(path, info);
    if (exists < 0 || (exists && !earth_regular(info->st_mode))) {
        fprintf(stderr, "e_bin: output must be a regular file: %s\n", path);
        return -1;
    }
    if (!exists) return 0;
    for (i = 0; i < sizeof earth_chunks / sizeof earth_chunks[0]; ++i) {
        if (!earth_path(input, directory, earth_chunks[i].file)) {
            fputs("e_bin: input path is too long\n", stderr);
            return -1;
        }
        if (earth_stat(input, &input_info) != 0) {
            fprintf(stderr, "e_bin: cannot examine input map: %s\n", input);
            return -1;
        }
        exists = earth_same_file(path, info, input, &input_info);
        if (exists < 0) {
            fputs("e_bin: cannot resolve input/output paths\n", stderr);
            return -1;
        }
        if (exists) {
            fprintf(stderr, "e_bin: output must not replace an input map: %s\n", path);
            return -1;
        }
    }
    return 1;
}

/* Reserve a new name without following links or truncating an existing file. */
static FILE *earth_create(const char *path)
{
    int descriptor;
    FILE *stream;
    descriptor = earth_open(path);
    if (descriptor < 0) return NULL;
    stream = earth_fdopen(descriptor, "wb");
    if (stream == NULL) {
        earth_close(descriptor);
        remove(path);
    }
    return stream;
}

/* DOS rename already refuses an existing destination. POSIX rename replaces
 * one, so publish a hard link first: neither an installation nor its rollback
 * may overwrite a file that appeared after the path checks. Both names are
 * siblings, which keeps the link on the same filesystem. */
static int earth_move_new(const char *source, const char *destination)
{
#ifdef _MSC_VER
    return rename(source, destination) == 0;
#else
    if (link(source, destination) != 0) return 0;
    if (remove(source) != 0) {
        remove(destination);
        return 0;
    }
    return 1;
#endif
}

/* Chunk size describes the on-disk resource format. Changing it also requires
 * resplitting the maps and updating their chunk tables/resource definitions. */
static int earth_layout(void)
{
    unsigned int i;
    int longitude;
    unsigned long remaining;
    longitude = 0;
    remaining = 0;
    for (i = 0; i < sizeof earth_chunks / sizeof earth_chunks[0]; ++i) {
        if (earth_chunks[i].longitude != longitude) {
            if (remaining != 0) return 0;
            longitude = earth_chunks[i].longitude;
            remaining = (unsigned long)longitude * (longitude / 2U);
        }
        if (earth_chunks[i].size == 0 ||
            earth_chunks[i].size > TG_EARTH_CHUNK_SIZE ||
            earth_chunks[i].size > remaining) return 0;
        remaining -= earth_chunks[i].size;
        if (remaining != 0 && earth_chunks[i].size != TG_EARTH_CHUNK_SIZE)
            return 0;
    }
    return remaining == 0;
}

static int earth_check(const char *path, unsigned long size)
{
    FILE *input;
    Earth_stat info;
    unsigned long i;
    int value, valid;
    if (earth_stat(path, &info) != 0 || !earth_regular(info.st_mode)) {
        fprintf(stderr, "e_bin: input must be a regular file: %s\n", path);
        return 0;
    }
    input = fopen(path, "rb");
    if (input == NULL) {
        fprintf(stderr, "e_bin: cannot read %s\n", path);
        return 0;
    }
    valid = 1;
    for (i = 0; i < size; ++i) {
        value = getc(input);
        if (value == EOF) {
            valid = 0;
            break;
        }
    }
    if (valid && getc(input) != EOF) valid = 0;
    if (ferror(input)) valid = 0;
    if (fclose(input) != 0) valid = 0;
    if (!valid)
        fprintf(stderr, "e_bin: %s must contain exactly %lu bytes\n", path, size);
    return valid;
}

static int earth_emit_chunk(FILE *output, const char *directory,
                            const struct Earth_chunk *chunk)
{
    static char path[EARTH_PATH_MAX];
    FILE *input;
    unsigned long i;
    int value, valid;
    if (!earth_path(path, directory, chunk->file)) return 0;
    input = fopen(path, "rb");
    if (input == NULL) return 0;
    fprintf(output, "#ifdef EARTH_%d\nstatic const unsigned char %s[] = {\n",
            chunk->longitude, chunk->name);
    valid = 1;
    for (i = 0; i < chunk->size; ++i) {
        value = getc(input);
        if (value == EOF) {
            valid = 0;
            break;
        }
        if (i % 16UL == 0) fputs("    ", output);
        fprintf(output, "%u", (unsigned int)value);
        if (i + 1UL != chunk->size) fputc(',', output);
        if (i % 16UL == 15UL || i + 1UL == chunk->size) fputc('\n', output);
    }
    if (valid && getc(input) != EOF) valid = 0;
    if (ferror(input)) valid = 0;
    if (fclose(input) != 0) valid = 0;
    fputs("};\n#endif\n\n", output);
    if (!valid) fprintf(stderr, "e_bin: input changed while reading %s\n", path);
    return valid && !ferror(output);
}

static void earth_emit_reader(FILE *output)
{
    unsigned int i, j, count;
    int longitude;
    count = (unsigned int)(sizeof earth_chunks / sizeof earth_chunks[0]);
    fputs("unsigned long earth_data_size(int longitude)\n{\n"
          "    switch (longitude) {\n", output);
    longitude = 0;
    for (i = 0; i < count; ++i) {
        if (earth_chunks[i].longitude == longitude) continue;
        longitude = earth_chunks[i].longitude;
        fprintf(output, "#ifdef EARTH_%d\n    case %d: return %luUL;\n#endif\n",
                longitude, longitude,
                (unsigned long)longitude * ((unsigned int)longitude / 2U));
    }
    fputs("    default: return 0;\n    }\n}\n\n"
          "static const unsigned char TG_HUGE *earth_data_chunk(int longitude,\n"
          "                                                     unsigned int chunk)\n"
          "{\n    switch (longitude) {\n", output);
    for (i = 0; i < count; i = j) {
        longitude = earth_chunks[i].longitude;
        fprintf(output, "#ifdef EARTH_%d\n    case %d:\n        switch (chunk) {\n",
                longitude, longitude);
        for (j = i; j < count && earth_chunks[j].longitude == longitude; ++j)
            fprintf(output, "        case %u: return %s;\n", j - i, earth_chunks[j].name);
        fputs("        default: return NULL;\n        }\n#endif\n", output);
    }
    fputs("    default: return NULL;\n    }\n}\n\n"
          "/* Return only bytes belonging to the same static chunk object. */\n"
          "const unsigned char TG_HUGE *earth_data_span(int longitude,\n"
          "                       unsigned long offset, unsigned int *available)\n"
          "{\n"
          "    unsigned long size, remaining;\n"
          "    unsigned int position;\n"
          "    const unsigned char TG_HUGE *source;\n"
          "    if (available == NULL) return NULL;\n"
          "    *available = 0;\n"
          "    size = earth_data_size(longitude);\n", output);
    fputs("    if (offset >= size) return NULL;\n"
          "    source = earth_data_chunk(longitude,\n"
          "                    (unsigned int)(offset / TG_EARTH_CHUNK_SIZE));\n"
          "    if (source == NULL) return NULL;\n"
          "    position = (unsigned int)(offset % TG_EARTH_CHUNK_SIZE);\n"
          "    remaining = TG_EARTH_CHUNK_SIZE - position;\n"
          "    if (remaining > size - offset) remaining = size - offset;\n"
          "    *available = (unsigned int)remaining;\n"
          "    return source + position;\n"
          "}\n\n", output);
    fputs("/* Count is bounded by unsigned int; offsets retain all 128 KiB.\n"
          " * Copy each <=16 KiB object separately, using normalized huge pointers\n"
          " * for both source and destination on segmented Microsoft builds. */\n", output);
    fputs("int earth_read_data(int longitude, unsigned long offset,\n"
          "                    unsigned int count,\n"
          "                    unsigned char TG_HUGE *destination)\n"
          "{\n"
          "    unsigned long size;\n"
          "    unsigned int position, length, remaining;\n"
          "    const unsigned char TG_HUGE *source;\n"
          "    size = earth_data_size(longitude);\n"
          "    if (size == 0 || offset > size ||\n"
          "        (unsigned long)count > size - offset ||\n"
          "        (count != 0 && destination == NULL)) return 0;\n", output);
    fputs("    while (count != 0) {\n"
          "        source = earth_data_chunk(longitude,\n"
          "                     (unsigned int)(offset / TG_EARTH_CHUNK_SIZE));\n"
          "        if (source == NULL) return 0;\n"
          "        position = (unsigned int)(offset % TG_EARTH_CHUNK_SIZE);\n"
          "        length = (unsigned int)(TG_EARTH_CHUNK_SIZE - position);\n"
          "        if (length > count) length = count;\n"
          "        source += position;\n"
          "        remaining = length;\n", output);
    fputs("        while (remaining != 0) {\n"
          "            *destination++ = *source++;\n"
          "            --remaining;\n"
          "        }\n"
          "        offset += length;\n"
          "        count -= length;\n"
          "    }\n"
          "    return 1;\n"
          "}\n\n#endif /* EARTH && !DRV_EARTH_RESOURCES */\n", output);
}

int main(int argc, char **argv)
{
    static char path[EARTH_PATH_MAX], temporary[EARTH_PATH_MAX];
    static char backup[EARTH_PATH_MAX];
    FILE *output;
    Earth_stat initial, current, sibling;
    unsigned int i, count;
    int valid, had_output, has_output;
    if (argc != 3) {
        fputs("Usage: e_bin data_directory output_file\n", stderr);
        return EXIT_FAILURE;
    }
    if (!earth_sibling(temporary, argv[2], ".tmp") ||
        !earth_sibling(backup, argv[2], ".bak") ||
        earth_entry(temporary, &sibling) != 0 ||
        earth_entry(backup, &sibling) != 0) {
        fputs("e_bin: output requires unused .tmp and .bak sibling names\n", stderr);
        return EXIT_FAILURE;
    }
    if (!earth_layout()) {
        fputs("e_bin: chunk size disagrees with the Earth map layout\n", stderr);
        return EXIT_FAILURE;
    }
    had_output = earth_output(argv[1], argv[2], &initial);
    if (had_output < 0) return EXIT_FAILURE;
    count = (unsigned int)(sizeof earth_chunks / sizeof earth_chunks[0]);
    for (i = 0; i < count; ++i) {
        if (!earth_path(path, argv[1], earth_chunks[i].file)) {
            fputs("e_bin: input path is too long\n", stderr);
            return EXIT_FAILURE;
        }
        if (!earth_check(path, earth_chunks[i].size)) return EXIT_FAILURE;
    }
    output = earth_create(temporary);
    if (output == NULL) {
        fprintf(stderr, "e_bin: cannot write %s\n", temporary);
        return EXIT_FAILURE;
    }
    fputs("/* Generated by e_bin.c from the data directory binaries; do not edit.\n"
          " * Copyright (C) 2026 trguhq\n"
          " * SPDX-License-Identifier: GPL-3.0-or-later\n"
          " * Each private array fits within one 16 KiB data chunk. */\n"
          "#include \"e_ext.h\"\n#include \"earthres.h\"\n\n", output);
    fprintf(output, "#if TG_EARTH_CHUNK_SIZE != %luUL\n"
                    "#error Regenerate Earth data after changing the chunk format\n"
                    "#endif\n\n", (unsigned long)TG_EARTH_CHUNK_SIZE);
    fputs("#if defined(EARTH) && !defined(DRV_EARTH_RESOURCES)\n\n", output);
    valid = 1;
    for (i = 0; i < count && valid; ++i)
        valid = earth_emit_chunk(output, argv[1], &earth_chunks[i]);
    if (valid) earth_emit_reader(output);
    if (ferror(output)) valid = 0;
    if (fclose(output) != 0) valid = 0;
    if (!valid) {
        remove(temporary);
        fputs("e_bin: generation failed; previous output preserved\n", stderr);
        return EXIT_FAILURE;
    }
    has_output = earth_output(argv[1], argv[2], &current);
    if (has_output < 0 || has_output != had_output ||
        (had_output &&
         (earth_same_file(argv[2], &initial, argv[2], &current) != 1 ||
          initial.st_size != current.st_size ||
          initial.st_mtime != current.st_mtime))) {
        remove(temporary);
        fputs("e_bin: output changed during generation; not replaced\n", stderr);
        return EXIT_FAILURE;
    }
    if (had_output && !earth_move_new(argv[2], backup)) {
        remove(temporary);
        fprintf(stderr, "e_bin: cannot preserve %s\n", argv[2]);
        return EXIT_FAILURE;
    }
    if (!earth_move_new(temporary, argv[2])) {
        if (had_output && !earth_move_new(backup, argv[2]))
            fprintf(stderr, "e_bin: previous output remains in %s\n", backup);
        remove(temporary);
        fprintf(stderr, "e_bin: cannot install %s\n", argv[2]);
        return EXIT_FAILURE;
    }
    if (had_output && remove(backup) != 0) {
        fprintf(stderr, "e_bin: cannot remove backup %s\n", backup);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
