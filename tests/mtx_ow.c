/*
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Matrox cross-build ABI check. Link the real LSCL.LIB with src/mtx_ow.c
 * using Open Watcom -bt=dos -mh -0 -fpi -ecc -dDRV_MTX and the SDK include
 * root. Compile this file and src/mtx_ow.c into differently named objects.
 * Run once with NO87 unset and argument "hardware", then with NO87=1 and
 * argument "emulated". The test checks the runtime's actual x87 selection.
 *
 * Real world-window, V3-polygon and screen-polygon bindings execute, with
 * a recorded ioputbuf/wrfile to avoid board access. The real ioputfbuf also
 * converts the V3 IEEE floats to 16.16. This validates Microsoft/Watcom
 * arguments, pointers, structs, returns and math, not graphics hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "XGX1/HEADERS/DEF.H"
#define PROTO
#include "XGX1/HEADERS/LIBFN.H"

/* Public SDK globals otherwise supplied by LSINIT.c. Defining them here
 * prevents hardware initialization from entering this numerical test. */
short buff[512];
short *ptrbuff;
short ioseg, globcnt;
short pgcfile = 1; /* Select IOPUTFBUF's file-output branch, never device I/O. */
static unsigned int mtx_test_calls;
static unsigned int mtx_test_count;
static unsigned short mtx_test_words[128];
static long mtx_test_expected[4];

/* Watcom's emulator sets __real87 to zero while exposing a virtual 387
 * through __8087. Its __dos87real flag retains physical detection. Check
 * actual runtime selection, not merely the reported physical hardware. */
extern unsigned char mtx_test_runtime87;
#pragma aux mtx_test_runtime87 "__real87";

void __cdecl ioputbuf(short *words, unsigned short count)
{
    unsigned int i;
    assert(count <= 128U - mtx_test_count);
    for (i = 0; i < count; ++i)
        mtx_test_words[mtx_test_count++] = (unsigned short)words[i];
    ++mtx_test_calls;
}

void __cdecl wrfile(short word)
{
    assert(mtx_test_count < 128U);
    mtx_test_words[mtx_test_count++] = (unsigned short)word;
}

static void mtx_test_window(void)
{
    unsigned int i;
    unsigned long actual;
    assert(mtx_test_count == 9U && mtx_test_words[0] == 0x8fU);
    for (i = 0; i < 4U; ++i)
    {
        actual = ((unsigned long)mtx_test_words[2U * i + 2U] << 16) |
                  mtx_test_words[2U * i + 1U];
        assert(actual == (unsigned long)mtx_test_expected[i]);
    }
    mtx_test_count = 0U;
}

extern long __far mtx_test_ftol(double value);
#pragma aux mtx_test_ftol "__ftol" parm [8087] value [dx ax] modify [bx cx];
extern char * __cdecl mtx_test_getenv(const char *name);
#pragma aux mtx_test_getenv "_getenv";
extern void * __cdecl mtx_test_malloc(size_t size);
#pragma aux mtx_test_malloc "_malloc";
extern int __cdecl mtx_test_printf(const char *format, ...);
#pragma aux mtx_test_printf "_printf";
extern unsigned long __cdecl mtx_test_strtoul(const char *text, char **end, int base);
#pragma aux mtx_test_strtoul "_strtoul";
extern char * __cdecl mtx_test_strcpy(char *destination, const char *source);
#pragma aux mtx_test_strcpy "_strcpy";
extern char * __cdecl mtx_test_strtok(char *text, const char *delimiters);
#pragma aux mtx_test_strtok "_strtok";
extern size_t __cdecl mtx_test_strlen(const char *text);
#pragma aux mtx_test_strlen "_strlen";
extern int __cdecl mtx_test_write(int file, const void *buffer, unsigned int count);
#pragma aux mtx_test_write "_write";
extern void __cdecl mtx_test_exit(int status);
#pragma aux mtx_test_exit "_exit";

int main(int argc, char **argv)
{
    static const double values[] = {
        0.0, 1.0, -1.0, 1.75, -1.75, 0.99, -0.99,
        16383.25, -16383.25, 2147483646.75, -2147483647.75
    };
    static const long expected[] = {
        0L, 1L, -1L, 1L, -1L, 0L, 0L,
        16383L, -16383L, 2147483646L, -2147483647L
    };
    static struct V3coord world[4] = {
        { 1.5f, -2.25f, 3.0f }, { -4.5f, 5.75f, -6.25f },
        { 1000.0f, -1000.0f, 0.000030517578125f }, { -0.25f, 0.125f, 0.0f }
    };
    static const long world_expected[12] = {
        98304L, -147456L, 196608L, -294912L, 376832L, -409600L,
        65536000L, -65536000L, 2L, -16384L, 8192L, 0L
    };
    static struct Phcoord screen[4] = {
        { 0, 0 }, { 639, 0 }, { 639, 479 }, { 0, 479 }
    };
    unsigned int i;
    unsigned long coordinate;
    char *memory, *end, *token;
    assert(argc == 2);
    if (strcmp(argv[1], "emulated") == 0) assert(mtx_test_runtime87 == 0);
    else
    {
        assert(strcmp(argv[1], "hardware") == 0);
        assert(mtx_test_runtime87 != 0);
    }
    assert(sizeof(struct V3coord) == 12U && sizeof(struct Phcoord) == 4U);
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        assert(mtx_test_ftol(values[i]) == expected[i]);

    mtx_test_expected[0] = -104857600L; mtx_test_expected[1] = 104857600L;
    mtx_test_expected[2] = -78643200L; mtx_test_expected[3] = 78643200L;
    v2gpswdw(-1600.0, 1600.0, -1200.0, 1200.0);
    mtx_test_window();
    mtx_test_expected[0] = 98304L; mtx_test_expected[1] = -98304L;
    mtx_test_expected[2] = 1L; mtx_test_expected[3] = -1L;
    v2gpswdw(1.5, -1.5, 1.0 / 65536.0, -1.0 / 65536.0);
    mtx_test_window();
    mtx_test_expected[0] = -2147418112L; mtx_test_expected[1] = 2147418112L;
    mtx_test_expected[2] = -16384L; mtx_test_expected[3] = 16384L;
    v2gpswdw(-32767.0, 32767.0, -0.25, 0.25);
    mtx_test_window();
    assert(mtx_test_calls == 3U);
    v3arwplygn(4U, world);
    assert(mtx_test_count == 26U && mtx_test_words[0] == 0x115U &&
           mtx_test_words[1] == 4U);
    for (i = 0; i < 12U; ++i)
    {
        coordinate = ((unsigned long)mtx_test_words[3U + 2U * i] << 16) |
                       mtx_test_words[2U + 2U * i];
        assert(coordinate == (unsigned long)world_expected[i]);
    }
    mtx_test_count = 0U;
    scarwplygn(4U, screen);
    assert(mtx_test_count == 10U && mtx_test_words[0] == 0x52U &&
           mtx_test_words[1] == 4U);
    for (i = 0; i < 4U; ++i)
    {
        assert(mtx_test_words[2U + 2U * i] == (unsigned short)screen[i].x);
        assert(mtx_test_words[3U + 2U * i] == (unsigned short)screen[i].y);
    }

    memory = (char *)mtx_test_malloc(48U);
    assert(memory != NULL);
    assert(mtx_test_strcpy(memory, "alpha:beta") == memory);
    assert(mtx_test_strlen(memory) == 10U);
    token = mtx_test_strtok(memory, ":");
    assert(token == memory && strcmp(token, "alpha") == 0);
    token = mtx_test_strtok(NULL, ":");
    assert(token != NULL && strcmp(token, "beta") == 0);
    assert(mtx_test_strtok(NULL, ":") == NULL);
    assert(mtx_test_strtoul("0xffffffff!", &end, 0) == 0xffffffffUL);
    assert(*end == '!');
    assert(mtx_test_getenv("PATH") != NULL);
    assert(mtx_test_printf("ABI values: %ld %s\n", -123456789L, memory) > 0);
    free(memory);
    assert(mtx_test_write(1, "ABI write\n", 10U) == 10);
    printf("Watcom runtime: %s\n", mtx_test_runtime87 ? "x87" : "emulated");
    puts("PASS LibShell/Watcom numeric, pointer, vararg and runtime adapters");
    mtx_test_exit(EXIT_SUCCESS);
    return EXIT_FAILURE;
}
