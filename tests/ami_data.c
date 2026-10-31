/* Exercise the production Amiga own-executable reader through DOS call records. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Amiga.h>
#include "testglob.h"
#include "drv.h"
#include "earth_resources.h"
#include "../src/ami_data.c"

static struct DosLibrary dos_library;
static struct Process process_record;
static struct CommandLineInterface cli_record;
static Ami_command_path command_path;
static union { unsigned long align; unsigned char bytes[256]; } command_name;
static struct WBStartup wb_startup;
static struct WBArg wb_arg;
static FILE *file_record;
static char executable[1024], basename_record[256];
static BPTR current_lock = 2;
static unsigned int opens, closes, library_live, maximum_read, data_reads;
static int forced_relative, short_read, fail_open;

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{ assert(strcmp(name, "dos.library") == 0 && version == 33); ++library_live; return (struct Library *)&dos_library; }
void CloseLibrary(struct Library *library)
{ assert(library == (struct Library *)&dos_library && library_live == 1); --library_live; }
struct Task *FindTask(CONST_STRPTR name) { assert(name == NULL); return (struct Task *)&process_record; }
BPTR CurrentDir(BPTR lock) { BPTR old; old = current_lock; current_lock = lock; return old; }
BPTR GetProgramDir(void) { assert(dos_library.dl_lib.lib_Version >= 36); return 1; }
BPTR Open(STRPTR name, LONG mode)
{
    assert(mode == MODE_OLDFILE && file_record == NULL);
    if (fail_open) return 0;
    if (strcmp(name, executable) != 0 && !(current_lock == 1 && strcmp(name, basename_record) == 0)) return 0;
    if (forced_relative && current_lock != 1) return 0;
    file_record = fopen(executable, "rb");
    if (file_record == NULL) return 0;
    ++opens; return 3;
}
LONG Close(BPTR file)
{ assert(file == 3 && file_record != NULL); assert(fclose(file_record) == 0); file_record = NULL; ++closes; return 1; }
LONG Seek(BPTR file, LONG offset, LONG mode)
{
    long previous;
    assert(file == 3 && file_record != NULL);
    previous = ftell(file_record); assert(previous >= 0);
    if (fseek(file_record, offset, mode == OFFSET_BEGINNING ? SEEK_SET : mode == OFFSET_END ? SEEK_END : SEEK_CUR) != 0) return -1;
    return previous;
}
LONG Read(BPTR file, APTR buffer, LONG length)
{
    assert(file == 3 && file_record != NULL && length > 0 && length <= 16384);
    if ((unsigned long)length > maximum_read) maximum_read = (unsigned int)length;
    if (length >= 512) ++data_reads;
    if (short_read && length >= 512) { short_read = 0; return length - 1; }
    return (LONG)fread(buffer, 1, (size_t)length, file_record);
}

int main(int argc, char **argv)
{
    char *launch[2];
    const char *mode, *name;
    unsigned char destination[16384], *canonical;
    unsigned long length;
    unsigned int id, entry, calls;
    assert(argc == 3 && strlen(argv[1]) < sizeof(executable));
    strcpy(executable, argv[1]); mode = argv[2];
    name = strrchr(executable, '/'); name = name == NULL ? executable : name + 1;
    assert(strlen(name) < sizeof(basename_record)); strcpy(basename_record, name);
    dos_library.dl_lib.lib_Version = 33;
    launch[0] = executable; launch[1] = NULL;
    process_record.pr_CLI = (BPTR)((ULONG)&cli_record >> 2);
    if (strcmp(mode, "malformed") == 0)
    {
        assert(ami_data_init(1, launch) == DRV_BAD_DATA);
        assert(file_record == NULL && opens == closes && current_lock == 2);
        ami_data_close(); assert(library_live == 0);
        puts("PASS Amiga data malformed: invalid footer/directory fails before map reads and closes every native handle");
        return 0;
    }
    if (strcmp(mode, "workbench") == 0)
    {
        wb_arg.wa_Lock = 1; wb_arg.wa_Name = basename_record;
        wb_startup.sm_NumArgs = 1; wb_startup.sm_ArgList = &wb_arg;
        assert(ami_data_init(0, (char **)&wb_startup) == DRV_OK);
    }
    else
    {
        if (strcmp(mode, "programdir") == 0 || strcmp(mode, "clipath") == 0)
        {
            forced_relative = 1; launch[0] = basename_record;
            if (strcmp(mode, "programdir") == 0) dos_library.dl_lib.lib_Version = 36;
            else
            {
                command_path.next = 0; command_path.lock = 1;
                cli_record.cli_CommandDir = (BPTR)((ULONG)&command_path >> 2);
            }
        }
        if (strcmp(mode, "cliname") == 0)
        {
            assert(strlen(executable) < 256); command_name.bytes[0] = (unsigned char)strlen(executable);
            memcpy(command_name.bytes + 1, executable, strlen(executable));
            cli_record.cli_CommandName = (BPTR)((ULONG)&command_name >> 2); launch[0] = NULL;
        }
        assert(ami_data_init(1, launch) == DRV_OK);
    }
    assert(current_lock == 2 && maximum_read == 36 && data_reads == 0);
    for (entry = 0; entry < 13; ++entry)
    {
        id = ami_data_id(entry); canonical = test_earth_resource((short)id, &length);
        memset(destination, 0xa5, sizeof(destination));
        assert(drv_load_earth_chunk(id, length, destination) == DRV_OK);
        assert(memcmp(destination, canonical, (size_t)length) == 0); free(canonical);
    }
    assert(maximum_read == 16384 && data_reads == 13);
    calls = data_reads;
    assert(drv_load_earth_chunk(999, 512, destination) == DRV_BAD_DATA);
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 513, destination) == DRV_BAD_DATA);
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 512, NULL) == DRV_BAD_DATA);
    assert(data_reads == calls);
    short_read = 1;
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 512, destination) == DRV_BAD_DATA);
    assert(file_record == NULL);
    assert(drv_load_earth_chunk(TG_EARTH_32_ID, 512, destination) == DRV_OK);
    assert(opens == closes + 1 && current_lock == 2);
    ami_data_close(); assert(file_record == NULL && opens == closes && library_live == 0);
    fail_open = 1; assert(ami_data_init(1, launch) == DRV_UNAVAILABLE); ami_data_close();
    assert(library_live == 0 && current_lock == 2);
    printf("PASS Amiga data %s: own-executable lookup, canonical selected chunks, bounded reads, short-read recovery and ownership\n", mode);
    return 0;
}
