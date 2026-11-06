/* Amiga executable-resident, demand-loaded Earth resources.
 * Copyright (C) 2026 trguhq. SPDX-License-Identifier: GPL-3.0-or-later
 * ami_pack.py places these bytes in HUNK_DEBUG, skipped by DOS LoadSeg.
 * This reader retains only a file handle and directory bounds, never a
 * duplicate full map. All native reads are limited to one 16 KiB chunk. */
#include "testglob.h"
#include "types.h"
#include "earthres.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <proto/exec.h>
/* Keep this library ownership separate from the C runtime's DOSBase. */
#define DOSBase ami_DOSBase
#include <proto/dos.h>
#ifdef __VBCC__
/* Some NDK distributions' proto headers only select GCC/SAS pragmas. */
#include <inline/exec_protos.h>
#include <inline/dos_protos.h>
#endif
#include <string.h>

#if TG_EARTH_CHUNK_SIZE != 16384UL
#error Amiga packed Earth resources require 16384-byte chunks
#endif

struct DosLibrary *ami_DOSBase;
static BPTR ami_data_file;
static unsigned long ami_data_directory;
static unsigned long ami_data_start;
static int ami_data_argc;
static char **ami_data_argv;

/* Older AmigaDOS CLI path lists are BPTR-linked pairs of next/lock. */
typedef struct Ami_command_path
{
    BPTR next;
    BPTR lock;
} Ami_command_path;

static unsigned long ami_data_u32(const unsigned char *bytes)
{
    return ((unsigned long)bytes[0] << 24) |
           ((unsigned long)bytes[1] << 16) |
           ((unsigned long)bytes[2] << 8) | (unsigned long)bytes[3];
}

static unsigned int ami_data_id(unsigned int entry)
{
    if (entry == 0) return TG_EARTH_32_ID;
    if (entry == 1) return TG_EARTH_64_ID;
    if (entry == 2) return TG_EARTH_128_ID;
    if (entry < 5) return TG_EARTH_256_ID + entry - 3U;
    return TG_EARTH_512_ID + entry - 5U;
}

static unsigned long ami_data_length(unsigned int entry)
{
    if (entry == 0) return 512UL;
    if (entry == 1) return 2048UL;
    if (entry == 2) return 8192UL;
    return TG_EARTH_CHUNK_SIZE;
}

static void ami_data_close_file(void)
{
    if (ami_data_file != 0) Close(ami_data_file);
    ami_data_file = 0;
    ami_data_directory = ami_data_start = 0;
}

static int ami_data_read(unsigned long offset, unsigned long length,
                          unsigned char *destination)
{
    if (ami_data_file == 0 || offset > 0x7fffffffUL ||
        length > TG_EARTH_CHUNK_SIZE || destination == NULL) return FALSE;
    if (Seek(ami_data_file, (LONG)offset, OFFSET_BEGINNING) == -1L) return FALSE;
    return Read(ami_data_file, destination, (LONG)length) == (LONG)length;
}

static BPTR ami_data_open_at(BPTR lock, const char *name)
{
    BPTR old, file;
    if (lock == 0 || name == NULL || *name == 0) return 0;
    old = CurrentDir(lock);
    file = Open((STRPTR)name, MODE_OLDFILE);
    CurrentDir(old);
    return file;
}

static BPTR ami_data_open_program(void)
{
    struct Process *process;
    struct CommandLineInterface *cli;
    struct WBStartup *startup;
    Ami_command_path *node;
    const unsigned char *command;
    const char *name, *base, *cursor;
    BPTR file, path;
    unsigned int tries, length;
    char cli_name[256];
    if (ami_data_argc == 0 && ami_data_argv != NULL)
    {
        startup = (struct WBStartup *)ami_data_argv;
        if (startup->sm_NumArgs < 1 || startup->sm_ArgList == NULL) return 0;
        return ami_data_open_at(startup->sm_ArgList[0].wa_Lock,
                               (const char *)startup->sm_ArgList[0].wa_Name);
    }
    name = ami_data_argc > 0 && ami_data_argv != NULL ? ami_data_argv[0] : NULL;
    process = (struct Process *)FindTask(NULL);
    cli = process != NULL && process->pr_CLI != 0 ?
          (struct CommandLineInterface *)BADDR(process->pr_CLI) : NULL;
    if ((name == NULL || *name == 0) && cli != NULL && cli->cli_CommandName != 0)
    {
        command = (const unsigned char *)BADDR(cli->cli_CommandName);
        length = command[0];
        memcpy(cli_name, command + 1, length);
        cli_name[length] = 0;
        name = cli_name;
    }
    if (name == NULL || *name == 0) return 0;
    file = Open((STRPTR)name, MODE_OLDFILE);
    if (file != 0) return file;
    base = name;
    for (cursor = name; *cursor != 0; ++cursor)
        if (*cursor == '/' || *cursor == ':') base = cursor + 1;
    /* GetProgramDir is V36; Workbench 1.3 uses its launch lock or CLI path. */
    if (ami_DOSBase->dl_lib.lib_Version >= 36)
    {
        file = ami_data_open_at(GetProgramDir(), base);
        if (file != 0) return file;
    }
    path = cli != NULL ? cli->cli_CommandDir : 0;
    for (tries = 0; path != 0 && tries < 256U; ++tries)
    {
        node = (Ami_command_path *)BADDR(path);
        file = ami_data_open_at(node->lock, base);
        if (file != 0) return file;
        path = node->next;
    }
    /* C: is an implicit AmigaDOS command-search location on old systems. */
    if (strlen(base) <= sizeof(cli_name) - 3U)
    {
        /* base can point inside cli_name when argv[0] was unavailable. */
        memmove(cli_name + 2, base, strlen(base) + 1U);
        cli_name[0] = 'C';
        cli_name[1] = ':';
        return Open((STRPTR)cli_name, MODE_OLDFILE);
    }
    return 0;
}

static Drv_result ami_data_open(void)
{
    LONG end;
    unsigned long footer, offset, size;
    unsigned int entry;
    unsigned char bytes[36];
    ami_data_close_file();
    if (ami_DOSBase == NULL)
        ami_DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 33UL);
    if (ami_DOSBase == NULL) return DRV_UNAVAILABLE;
    ami_data_file = ami_data_open_program();
    if (ami_data_file == 0) return DRV_UNAVAILABLE;
    if (Seek(ami_data_file, 0L, OFFSET_END) == -1L) goto invalid;
    end = Seek(ami_data_file, 0L, OFFSET_CURRENT);
    if (end < 36L || ((unsigned long)end & 3UL) != 0) goto invalid;
    footer = (unsigned long)end - 36UL;
    if (!ami_data_read(footer, 36UL, bytes) ||
        memcmp(bytes, "TGEARTH1", 8) != 0 ||
        ami_data_u32(bytes + 8) != 1UL || ami_data_u32(bytes + 12) != 13UL ||
        ami_data_u32(bytes + 24) != 174592UL || ami_data_u32(bytes + 28) != 0UL ||
        ami_data_u32(bytes + 32) != 1010UL) goto invalid;
    ami_data_directory = ami_data_u32(bytes + 16);
    ami_data_start = ami_data_u32(bytes + 20);
    if (ami_data_start < 16UL || ami_data_start > footer ||
        footer - ami_data_start != 174592UL + 13UL * 12UL ||
        ami_data_directory != footer - 13UL * 12UL) goto invalid;
    if (!ami_data_read(ami_data_start - 16UL, 16UL, bytes) ||
        ami_data_u32(bytes) != 1009UL ||
        ami_data_u32(bytes + 4) != (footer + 32UL - ami_data_start + 8UL) / 4UL ||
        ami_data_u32(bytes + 8) != 0UL || memcmp(bytes + 12, "TGEO", 4) != 0)
        goto invalid;
    offset = ami_data_start;
    for (entry = 0; entry < 13U; ++entry)
    {
        size = ami_data_length(entry);
        if (!ami_data_read(ami_data_directory + 12UL * entry, 12UL, bytes) ||
            ami_data_u32(bytes) != (unsigned long)ami_data_id(entry) ||
            ami_data_u32(bytes + 4) != offset || ami_data_u32(bytes + 8) != size)
            goto invalid;
        offset += size;
    }
    if (offset != ami_data_directory) goto invalid;
    return DRV_OK;
invalid:
    ami_data_close_file();
    return DRV_BAD_DATA;
}

Drv_result ami_data_init(int argc, char **argv)
{
    ami_data_argc = argc;
    ami_data_argv = argv;
    return ami_data_open();
}

void ami_data_close(void)
{
    ami_data_close_file();
    if (ami_DOSBase != NULL) CloseLibrary((struct Library *)ami_DOSBase);
    ami_DOSBase = NULL;
    ami_data_argc = 0;
    ami_data_argv = NULL;
}

Drv_result drv_load_earth_chunk(unsigned int id, unsigned long length,
                                 unsigned char TG_HUGE *destination)
{
    Drv_result result;
    unsigned int entry;
    unsigned long offset;
    if (destination == NULL || length == 0 || length > TG_EARTH_CHUNK_SIZE)
        return DRV_BAD_DATA;
    for (entry = 0; entry < 13U && ami_data_id(entry) != id; ++entry) ;
    if (entry == 13U || length != ami_data_length(entry)) return DRV_BAD_DATA;
    if (ami_data_file == 0)
    {
        result = ami_data_open();
        if (result != DRV_OK) return result;
    }
    offset = ami_data_start;
    while (entry != 0U) offset += ami_data_length(--entry);
    if (!ami_data_read(offset, length, destination))
    {
        ami_data_close_file();
        return DRV_BAD_DATA;
    }
    return DRV_OK;
}
