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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EARTH_PATH_MAX 1024
#define EARTH_CHUNK_SIZE 16384UL

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
    { "e256_0.bin", "earth_data_256_0", 256, EARTH_CHUNK_SIZE },
    { "e256_1.bin", "earth_data_256_1", 256, EARTH_CHUNK_SIZE },
    { "e512_0.bin", "earth_data_512_0", 512, EARTH_CHUNK_SIZE },
    { "e512_1.bin", "earth_data_512_1", 512, EARTH_CHUNK_SIZE },
    { "e512_2.bin", "earth_data_512_2", 512, EARTH_CHUNK_SIZE },
    { "e512_3.bin", "earth_data_512_3", 512, EARTH_CHUNK_SIZE },
    { "e512_4.bin", "earth_data_512_4", 512, EARTH_CHUNK_SIZE },
    { "e512_5.bin", "earth_data_512_5", 512, EARTH_CHUNK_SIZE },
    { "e512_6.bin", "earth_data_512_6", 512, EARTH_CHUNK_SIZE },
    { "e512_7.bin", "earth_data_512_7", 512, EARTH_CHUNK_SIZE }
};

static int earth_path(char *path, const char *directory, const char *name)
{
    size_t length;
    length = strlen(directory);
    if (length + strlen(name) + 2U > EARTH_PATH_MAX) return 0;
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
    if (length + strlen(extension) + 1U > EARTH_PATH_MAX) return 0;
    memcpy(path, output, length);
    strcpy(path + length, extension);
    return strcmp(path, output) != 0;
}

static int earth_exists(const char *path)
{
    FILE *input;
    input = fopen(path, "rb");
    if (input == NULL) return 0;
    fclose(input);
    return 1;
}

static int earth_check(const char *path, unsigned long size)
{
    FILE *input;
    unsigned long i;
    int value, valid;
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
          "/* Count is bounded by unsigned int; offsets retain all 128 KiB.\n"
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
    unsigned int i, count;
    int valid, had_output;
    if (argc != 3) {
        fputs("Usage: e_bin data_directory output_file\n", stderr);
        return EXIT_FAILURE;
    }
    if (!earth_sibling(temporary, argv[2], ".tmp") ||
        !earth_sibling(backup, argv[2], ".bak") ||
        earth_exists(temporary) || earth_exists(backup)) {
        fputs("e_bin: output requires unused .tmp and .bak sibling names\n", stderr);
        return EXIT_FAILURE;
    }
    count = (unsigned int)(sizeof earth_chunks / sizeof earth_chunks[0]);
    for (i = 0; i < count; ++i) {
        if (!earth_path(path, argv[1], earth_chunks[i].file)) {
            fputs("e_bin: input path is too long\n", stderr);
            return EXIT_FAILURE;
        }
        if (!earth_check(path, earth_chunks[i].size)) return EXIT_FAILURE;
    }
    output = fopen(temporary, "wb");
    if (output == NULL) {
        fprintf(stderr, "e_bin: cannot write %s\n", temporary);
        return EXIT_FAILURE;
    }
    fputs("/* Generated by e_bin.c from the data directory binaries; do not edit.\n"
          " * Copyright (C) 2026 trguhq\n"
          " * SPDX-License-Identifier: GPL-3.0-or-later\n"
          " * Each private array fits within one 16 KiB data chunk. */\n"
          "#include \"e_ext.h\"\n#include \"earthres.h\"\n\n"
          "#if defined(EARTH) && !defined(DRV_EARTH_RESOURCES)\n\n", output);
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
    had_output = earth_exists(argv[2]);
    if (had_output && rename(argv[2], backup) != 0) {
        remove(temporary);
        fprintf(stderr, "e_bin: cannot preserve %s\n", argv[2]);
        return EXIT_FAILURE;
    }
    if (rename(temporary, argv[2]) != 0) {
        if (had_output && rename(backup, argv[2]) != 0)
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
