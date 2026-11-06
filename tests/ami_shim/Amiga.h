/* Recorded AmigaOS calls for host tests. This is not an NDK or an ABI shim. */
#ifndef TEST_AMIGA_H
#define TEST_AMIGA_H

#include <stddef.h>
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned long ULONG;
typedef long LONG;
typedef int BOOL;
typedef void VOID;
typedef void *APTR;
typedef const void *CONST_APTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
typedef unsigned char *PLANEPTR;
typedef unsigned long BPTR;

struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct Library { struct Node lib_Node; UWORD lib_Version, lib_Revision; };
struct DosLibrary { struct Library dl_lib; };
struct WBArg { BPTR wa_Lock; STRPTR wa_Name; };
struct WBStartup { LONG sm_NumArgs; struct WBArg *sm_ArgList; };
struct Task { APTR tc_SPLower, tc_SPUpper; };
struct Process { struct Task pr_Task; BPTR pr_CLI; };
struct CommandLineInterface { BPTR cli_CommandName, cli_CommandDir; };

struct ExecBase { struct Library LibNode; UWORD AttnFlags; };
struct GfxBase { struct Library LibNode; UBYTE ChipRevBits0; UWORD DisplayFlags; };
struct IntuitionBase { struct Library LibNode; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct MsgPort { struct Node mp_Node; UBYTE mp_Flags, mp_SigBit; APTR mp_SigTask; };
struct TextFont { UWORD tf_YSize; UBYTE tf_Style, tf_Flags; UWORD tf_XSize, tf_Baseline; };
struct TextAttr { STRPTR ta_Name; UWORD ta_YSize; UBYTE ta_Style, ta_Flags; };
struct BitMap { UWORD BytesPerRow, Rows; UBYTE Flags, Depth; UWORD pad; PLANEPTR Planes[8]; };
struct Layer { int unused; };
struct AreaInfo { WORD *VctrTbl, *VctrPtr; BYTE *FlagTbl, *FlagPtr; WORD Count, MaxCount, FirstX, FirstY; };
struct TmpRas { PLANEPTR RasPtr; LONG Size; };
struct RastPort {
    struct Layer *Layer;
    struct BitMap *BitMap;
    struct AreaInfo *AreaInfo;
    struct TmpRas *TmpRas;
    struct TextFont *Font;
    WORD cp_x, cp_y, TxHeight, TxWidth, TxBaseline;
    UWORD Flags;
    UBYTE FgPen, BgPen, AOlPen, DrawMode;
    UWORD *AreaPtrn;
    BYTE AreaPtSz;
};
struct ColorMap { UBYTE Flags, Type; UWORD Count; APTR ColorTable; };
struct ViewPort { struct ColorMap *ColorMap; UWORD Modes; };
struct Screen {
    struct Screen *NextScreen;
    WORD LeftEdge, TopEdge, Width, Height;
    UWORD Flags;
    struct TextAttr *Font;
    struct ViewPort ViewPort;
    struct RastPort RastPort;
    struct BitMap BitMap;
};
struct Window {
    struct Window *NextWindow;
    WORD LeftEdge, TopEdge, Width, Height;
    WORD GZZWidth, GZZHeight;
    BYTE BorderLeft, BorderTop, BorderRight, BorderBottom;
    struct Screen *WScreen;
    struct RastPort *RPort;
    struct MsgPort *UserPort;
    ULONG IDCMPFlags, Flags;
    WORD MouseX, MouseY;
};
struct NewWindow {
    WORD LeftEdge, TopEdge, Width, Height;
    UBYTE DetailPen, BlockPen;
    ULONG IDCMPFlags, Flags;
    APTR FirstGadget, CheckMark;
    UBYTE *Title;
    struct Screen *Screen;
    struct BitMap *BitMap;
    WORD MinWidth, MinHeight;
    UWORD MaxWidth, MaxHeight;
    UWORD Type;
};
struct Point { WORD x, y; };
struct DisplayInfo { struct Point Resolution; };
struct IntuiText { UBYTE FrontPen, BackPen, DrawMode; WORD LeftEdge, TopEdge; struct TextAttr *ITextFont; UBYTE *IText; struct IntuiText *NextText; };
struct IntuiMessage {
    struct Message ExecMessage;
    ULONG Class;
    UWORD Code, Qualifier;
    APTR IAddress;
    WORD MouseX, MouseY;
    ULONG Seconds, Micros;
    struct Window *IDCMPWindow;
    APTR SpecialLink;
};

#define BADDR(value) ((APTR)((ULONG)(value) << 2))
#define MODE_OLDFILE 1005
#define OFFSET_BEGINNING -1L
#define OFFSET_CURRENT 0L
#define OFFSET_END 1L
#define TRUE 1
#define FALSE 0
#define AFF_68010 (1U << 0)
#define AFF_68020 (1U << 1)
#define AFF_68030 (1U << 2)
#define AFF_68040 (1U << 3)
#define AFF_68881 (1U << 4)
#define AFF_68882 (1U << 5)
#define AFF_FPU40 (1U << 6)
#define AFF_68060 (1U << 7)
#define MEMF_ANY 0UL
#define MEMF_PUBLIC 1UL
#define MEMF_CHIP 2UL
#define MEMF_FAST 4UL
#define MEMF_CLEAR 0x10000UL
#define WBENCHSCREEN 1U
#define PUBLICSCREEN 2U
#define CUSTOMSCREEN 15U
#define WINDOWCLOSE 0x0008UL
#define WINDOWDRAG 0x0002UL
#define WINDOWDEPTH 0x0004UL
#define WINDOWSIZING 0x0001UL
#define ACTIVATE 0x1000UL
#define GIMMEZEROZERO 0x0400UL
#define SMART_REFRESH 0UL
#define SIMPLE_REFRESH 0x0040UL
#define REPORTMOUSE 0x0200UL
#define RMBTRAP 0x00010000UL
#define NOCAREREFRESH 0x20000UL
#define CLOSEWINDOW 0x0200UL
#define NEWSIZE 0x0002UL
#define REFRESHWINDOW 0x0004UL
#define MOUSEBUTTONS 0x0008UL
#define MOUSEMOVE 0x0010UL
#define VANILLAKEY 0x200000UL
#define RAWKEY 0x0400UL
#define ACTIVEWINDOW 0x40000UL
#define INACTIVEWINDOW 0x80000UL
#define INTUITICKS 0x400000UL
#define SELECTDOWN 0x68U
#define SELECTUP 0xe8U
#define MENUDOWN 0x69U
#define MENUUP 0xe9U
#define IEQUALIFIER_LBUTTON 0x4000U
#define IEQUALIFIER_RBUTTON 0x2000U
#define JAM1 0U
#define JAM2 1U
#define COMPLEMENT 2U
#define AREAOUTLINE 0x0008U
#define HAM 0x0800U
#define EXTRA_HALFBRITE 0x0080U
#define DUALPF 0x0400U
#define HIRES 0x8000U
#define LACE 0x0004U
#define INVALID_ID 0xffffffffUL
#define DTAG_DISP 0x80000000UL
#define PAL 0x0004U
#define GFXF_AA_ALICE 4U
#define GFXF_HR_AGNUS 1U
#define GFXF_HR_DENISE 2U
#define SIGBREAKF_CTRL_C (1UL << 12)
#define RASSIZE(w,h) (((((ULONG)(w) + 15UL) >> 4) << 1) * (ULONG)(h))
#define SetAfPt(rp,pattern,size) ((rp)->AreaPtrn = (pattern), (rp)->AreaPtSz = (size))
#define BMF_DISPLAYABLE 2UL
#define BMF_CLEAR 1UL
#define BMF_STANDARD 8UL
#define BMF_MINPLANES 16UL
#define BMA_HEIGHT 0UL
#define BMA_WIDTH 8UL
#define BMA_DEPTH 4UL
#define BMA_FLAGS 12UL

extern struct ExecBase *SysBase;
struct Library *OpenLibrary(CONST_STRPTR, ULONG);
void CloseLibrary(struct Library *);
struct Task *FindTask(CONST_STRPTR);
APTR AllocMem(ULONG, ULONG);
void FreeMem(APTR, ULONG);
struct Message *GetMsg(struct MsgPort *);
void ReplyMsg(struct Message *);
ULONG Wait(ULONG);
void CurrentTime(ULONG *, ULONG *);
LONG GetScreenData(APTR, ULONG, ULONG, struct Screen *);
struct Screen *LockPubScreen(CONST_STRPTR);
void UnlockPubScreen(CONST_STRPTR, struct Screen *);
struct Window *OpenWindow(const struct NewWindow *);
void CloseWindow(struct Window *);
void BeginRefresh(struct Window *);
void EndRefresh(struct Window *, LONG);
void InitBitMap(struct BitMap *, LONG, ULONG, ULONG);
struct BitMap *AllocBitMap(ULONG, ULONG, ULONG, ULONG, const struct BitMap *);
void FreeBitMap(struct BitMap *);
ULONG GetBitMapAttr(const struct BitMap *, ULONG);
PLANEPTR AllocRaster(ULONG, ULONG);
void FreeRaster(PLANEPTR, ULONG, ULONG);
void InitRastPort(struct RastPort *);
void InitArea(struct AreaInfo *, APTR, LONG);
struct TmpRas *InitTmpRas(struct TmpRas *, PLANEPTR, LONG);
void SetAPen(struct RastPort *, ULONG);
void SetBPen(struct RastPort *, ULONG);
void SetDrMd(struct RastPort *, ULONG);
void RectFill(struct RastPort *, LONG, LONG, LONG, LONG);
void Move(struct RastPort *, LONG, LONG);
void Text(struct RastPort *, CONST_STRPTR, ULONG);
WORD TextLength(struct RastPort *, CONST_STRPTR, ULONG);
LONG AreaMove(struct RastPort *, LONG, LONG);
LONG AreaDraw(struct RastPort *, LONG, LONG);
LONG AreaEnd(struct RastPort *);
void WaitBlit(void);
void LockLayerRom(struct Layer *);
void UnlockLayerRom(struct Layer *);
void BltBitMapRastPort(const struct BitMap *, LONG, LONG, struct RastPort *, LONG, LONG, LONG, LONG, ULONG);
ULONG GetRGB4(struct ColorMap *, LONG);
void GetRGB32(struct ColorMap *, ULONG, ULONG, ULONG *);
void SetFont(struct RastPort *, struct TextFont *);
ULONG GetVPModeID(struct ViewPort *);
ULONG GetDisplayInfoData(APTR, UBYTE *, ULONG, ULONG, ULONG);
BOOL AutoRequest(struct Window *, struct IntuiText *, struct IntuiText *, struct IntuiText *, ULONG, ULONG, WORD, WORD);

BPTR Open(STRPTR, LONG);
LONG Close(BPTR);
LONG Seek(BPTR, LONG, LONG);
LONG Read(BPTR, APTR, LONG);
BPTR CurrentDir(BPTR);
BPTR GetProgramDir(void);

#endif
