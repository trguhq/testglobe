/*
 * testglobe - Microsoft LibShell runtime adapters for Open Watcom DOS.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LSCL.LIB contains Microsoft C large-model objects. Its public calls use
 * far, stack-based cdecl arguments; the Watcom build uses -mh -ecc. The
 * runtime imports below have different names/calling conventions from
 * Watcom's own C library, so route them through explicit cdecl adapters.
 * This object is used only by matrox-cross, never by the Microsoft build.
 */
#if !defined(__WATCOMC__) || !defined(__I86__) || !defined(DRV_MTX)
#error mtx_ow.c requires the Open Watcom 16-bit Matrox target
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>

/* Link-retention markers referenced by Microsoft's objects. Watcom's
 * floating expressions below pull in its own floating-point startup.
 * Both compilers use the same FI*RQQ floating emulation fixups, which
 * Watcom's -fpi runtime handles on machines with or without an x87. */
int mtx_msc_fltused;
#pragma aux mtx_msc_fltused "__fltused";
int mtx_msc_acrtused;
#pragma aux mtx_msc_acrtused "__acrtused";

/* Microsoft __ftol consumes ST(0), truncates toward zero and returns a
 * signed long in DX:AX. Express that interface to the compiler instead
 * of emitting raw x87 instructions, preserving -fpi software emulation. */
long __far mtx_msc_ftol(double value);
#pragma aux mtx_msc_ftol "__ftol" parm [8087] value [dx ax] modify [bx cx];
long __far mtx_msc_ftol(double value)
{
    return (long)value;
}

char * __cdecl mtx_msc_getenv(const char *name);
#pragma aux mtx_msc_getenv "_getenv";
char * __cdecl mtx_msc_getenv(const char *name)
{
    return getenv(name);
}

void * __cdecl mtx_msc_malloc(size_t size);
#pragma aux mtx_msc_malloc "_malloc";
void * __cdecl mtx_msc_malloc(size_t size)
{
    return malloc(size);
}

int __cdecl mtx_msc_printf(const char *format, ...);
#pragma aux mtx_msc_printf "_printf";
int __cdecl mtx_msc_printf(const char *format, ...)
{
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

unsigned long __cdecl mtx_msc_strtoul(const char *text, char **end, int base);
#pragma aux mtx_msc_strtoul "_strtoul";
unsigned long __cdecl mtx_msc_strtoul(const char *text, char **end, int base)
{
    return strtoul(text, end, base);
}

char * __cdecl mtx_msc_strcpy(char *destination, const char *source);
#pragma aux mtx_msc_strcpy "_strcpy";
char * __cdecl mtx_msc_strcpy(char *destination, const char *source)
{
    return strcpy(destination, source);
}

char * __cdecl mtx_msc_strtok(char *text, const char *delimiters);
#pragma aux mtx_msc_strtok "_strtok";
char * __cdecl mtx_msc_strtok(char *text, const char *delimiters)
{
    return strtok(text, delimiters);
}

size_t __cdecl mtx_msc_strlen(const char *text);
#pragma aux mtx_msc_strlen "_strlen";
size_t __cdecl mtx_msc_strlen(const char *text)
{
    return strlen(text);
}

void __cdecl mtx_msc_exit(int status);
#pragma aux mtx_msc_exit "_exit";
void __cdecl mtx_msc_exit(int status)
{
    exit(status);
}

int __cdecl mtx_msc_write(int file, const void *buffer, unsigned int count);
#pragma aux mtx_msc_write "_write";
int __cdecl mtx_msc_write(int file, const void *buffer, unsigned int count)
{
    return write(file, buffer, count);
}
