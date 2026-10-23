/* Manual-derived System 7 call recorder contract. NOT MPW SDK or ABI. */
#ifndef TEST_QD_CLASSIC_H
#define TEST_QD_CLASSIC_H
#include "Types.h"
typedef OSType ResType;
typedef long Fixed;
typedef short QDErr;
typedef unsigned char Style;
typedef struct { short v, h; } Point;
typedef struct { short top, left, bottom, right; } Rect;
typedef struct { unsigned char pat[8]; } Pattern;
typedef struct { unsigned short red, green, blue; } RGBColor;
typedef struct { short value; RGBColor rgb; } ColorSpec;
typedef struct { long ctSeed; short ctFlags, ctSize; ColorSpec ctTable[1]; } ColorTable;
typedef ColorTable **CTabHandle;
typedef struct { Ptr baseAddr; short rowBytes; Rect bounds; } BitMap;
typedef struct {
    Ptr baseAddr; short rowBytes; Rect bounds;
    short pmVersion, packType; long packSize; Fixed hRes, vRes;
    short pixelType, pixelSize, cmpCount, cmpSize; long planeBytes;
    CTabHandle pmTable; long pmReserved;
} PixMap;
typedef PixMap **PixMapHandle;
typedef struct { RGBColor color; } PixPat;
typedef PixPat **PixPatHandle;
typedef struct Region { short rgnSize; Rect rgnBBox; } Region;
typedef Region **RgnHandle;
typedef struct GrafPort {
    short device; BitMap portBits; Rect portRect;
    RgnHandle visRgn, clipRgn;
} GrafPort;
typedef GrafPort *GrafPtr;
typedef struct CGrafPort {
    short device; BitMap portBits; Rect portRect;
    RgnHandle visRgn, clipRgn; PixMapHandle portPixMap;
} CGrafPort;
typedef CGrafPort *CGrafPtr;
typedef CGrafPtr GWorldPtr;
typedef GrafPtr WindowPtr;
typedef CGrafPtr CWindowPtr;
typedef GrafPort WindowRecord;
typedef CGrafPort CWindowRecord;
typedef struct GDevice {
    short gdRefNum, gdID, gdType; Handle gdITable; short gdResPref;
    PixMapHandle gdPMap; long gdRefCon; struct GDevice **gdNextGD;
    Rect gdRect; long gdMode; short gdCCBytes, gdCCDepth;
    Handle gdCCXData, gdCCXMask; long gdReserved;
} GDevice;
typedef GDevice **GDHandle;
typedef struct { short polySize; Rect polyBBox; Point polyPoints[1]; } Polygon;
typedef Polygon **PolyHandle;
typedef struct MenuInfo { short menuID; } MenuInfo;
typedef MenuInfo **MenuHandle;
typedef struct Palette { short entries; } Palette;
typedef Palette **PaletteHandle;
typedef unsigned short EventKind;
typedef unsigned short EventMask;
typedef unsigned short EventModifiers;
typedef struct { EventKind what; unsigned long message, when; Point where; EventModifiers modifiers; } EventRecord;
typedef struct Cursor { short data[16], mask[16]; Point hotSpot; } Cursor;
typedef struct { long randSeed; BitMap screenBits; Cursor arrow; Pattern dkGray, ltGray, gray, black, white; GrafPtr thePort; } QDGlobals;
extern QDGlobals qd;
#define noErr 0
#define memFullErr (-108)
#define gestaltQuickdrawVersion 0x71642020UL
#define gestaltProcessorType 0x63707520UL
#define gestaltFPUType 0x66707520UL
#define gestaltOriginalQD 0x0000
#define gestalt8BitQD 0x0100
#define gestalt32BitQD 0x0200
#define gestalt32BitQD11 0x0210
#define gestalt32BitQD12 0x0220
#define gestalt32BitQD13 0x0230
#define gestalt68000 1
#define gestalt68010 2
#define gestalt68020 3
#define gestalt68030 4
#define gestalt68040 5
#define gestaltNoFPU 0
#define gestalt68881 1
#define gestalt68882 2
#define gestalt68040FPU 3
#define screenDevice 0
#define screenActive 1
#define mainScreen 2
#define blackColor 33L
#define whiteColor 30L
#define srcCopy 0
#define ditherCopy 64
#define patCopy 8
#define useTempMem 4UL
#define noNewDevice 2UL
#define keepLocal 8UL
#define pmTolerant 0x0002
#define pmExplicit 0x0008
#define pmAnimated 0x0004
#define pmCourteous 0x0000
#define noUpdates 0
#define activate 1
#define deactivate 0
#define everyEvent 0xffffU
#define mouseDown 1
#define mouseUp 2
#define keyDown 3
#define keyUp 4
#define autoKey 5
#define updateEvt 6
#define diskEvt 7
#define activateEvt 8
#define osEvt 15
#define nullEvent 0
#define charCodeMask 0x000000ffL
#define keyCodeMask 0x0000ff00L
#define cmdKey 0x0100
#define activeFlag 1
#define suspendResumeMessage 1
#define resumeFlag 1
#define inDesk 0
#define inMenuBar 1
#define inSysWindow 2
#define inContent 3
#define inDrag 4
#define inGrow 5
#define inGoAway 6
#define inZoomIn 7
#define inZoomOut 8
#define documentProc 0
#define zoomDocProc 8
#define systemFont 0

Ptr NewPtr(Size);
void DisposePtr(Ptr);
Handle NewHandle(Size);
void DisposeHandle(Handle);
void HLock(Handle);
void HUnlock(Handle);
Size GetHandleSize(Handle);
Handle Get1Resource(OSType, short);
void ReleaseResource(Handle);
OSErr MemError(void);
unsigned long TickCount(void);
void InitGraf(GrafPtr *);
void InitFonts(void);
void InitWindows(void);
void InitMenus(void);
void TEInit(void);
void InitDialogs(void *);
void InitCursor(void);
void MaxApplZone(void);
OSErr Gestalt(OSType, long *);
GDHandle GetDeviceList(void);
GDHandle GetNextDevice(GDHandle);
GDHandle GetMainDevice(void);
GDHandle GetGDevice(void);
void SetGDevice(GDHandle);
GDHandle GetMaxDevice(const Rect *);
Boolean TestDeviceAttribute(GDHandle, short);
WindowPtr NewWindow(Ptr, const Rect *, ConstStr255Param, Boolean, short, WindowPtr, Boolean, long);
WindowPtr NewCWindow(Ptr, const Rect *, ConstStr255Param, Boolean, short, WindowPtr, Boolean, long);
void DisposeWindow(WindowPtr);
void ShowWindow(WindowPtr);
void SelectWindow(WindowPtr);
WindowPtr FrontWindow(void);
void SetPort(GrafPtr);
void GetPort(GrafPtr *);
void SetGWorld(CGrafPtr, GDHandle);
void GetGWorld(CGrafPtr *, GDHandle *);
QDErr NewGWorld(GWorldPtr *, short, const Rect *, CTabHandle, GDHandle, unsigned long);
PixMapHandle GetGWorldPixMap(GWorldPtr);
Boolean LockPixels(PixMapHandle);
void UnlockPixels(PixMapHandle);
void DisposeGWorld(GWorldPtr);
void SetRect(Rect *, short, short, short, short);
void OffsetRect(Rect *, short, short);
void ClipRect(const Rect *);
RgnHandle NewRgn(void);
void DisposeRgn(RgnHandle);
void GetClip(RgnHandle);
void SetClip(RgnHandle);
void EraseRect(const Rect *);
void PaintRect(const Rect *);
PolyHandle OpenPoly(void);
void ClosePoly(void);
void KillPoly(PolyHandle);
void PaintPoly(PolyHandle);
void FillPoly(PolyHandle, const Pattern *);
void MoveTo(short, short);
void LineTo(short, short);
void PenSize(short, short);
void PenNormal(void);
void HidePen(void);
void ShowPen(void);
PixPatHandle NewPixPat(void);
void DisposePixPat(PixPatHandle);
void MakeRGBPat(PixPatHandle, const RGBColor *);
void FillCPoly(PolyHandle, PixPatHandle);
void ForeColor(long);
void BackColor(long);
void RGBForeColor(const RGBColor *);
void RGBBackColor(const RGBColor *);
void GetForeColor(RGBColor *);
void GetBackColor(RGBColor *);
void TextFont(short);
void TextSize(short);
void TextMode(short);
short TextWidth(const void *, short, short);
void DrawText(const void *, short, short);
void DrawString(ConstStr255Param);
void CopyBits(const BitMap *, const BitMap *, const Rect *, const Rect *, short, RgnHandle);
long GetCTSeed(void);
PaletteHandle NewPalette(short, CTabHandle, short, short);
void SetPalette(WindowPtr, PaletteHandle, Boolean);
void ActivatePalette(WindowPtr);
void DisposePalette(PaletteHandle);
void NSetPalette(WindowPtr, PaletteHandle, short);
MenuHandle NewMenu(short, ConstStr255Param);
void AppendMenu(MenuHandle, ConstStr255Param);
void InsertMenu(MenuHandle, short);
void DeleteMenu(short);
void DisposeMenu(MenuHandle);
void DrawMenuBar(void);
void ClearMenuBar(void);
void DrawGrowIcon(WindowPtr);
long MenuSelect(Point);
long MenuKey(short);
void HiliteMenu(short);
Boolean WaitNextEvent(EventMask, EventRecord *, unsigned long, RgnHandle);
short FindWindow(Point, WindowPtr *);
void DragWindow(WindowPtr, Point, const Rect *);
long GrowWindow(WindowPtr, Point, const Rect *);
void SizeWindow(WindowPtr, short, short, Boolean);
Boolean TrackGoAway(WindowPtr, Point);
void BeginUpdate(WindowPtr);
void EndUpdate(WindowPtr);
void SystemClick(const EventRecord *, WindowPtr);
void SystemTask(void);
Boolean Button(void);
void GetMouse(Point *);
void GlobalToLocal(Point *);
void LocalToGlobal(Point *);
void ParamText(ConstStr255Param, ConstStr255Param, ConstStr255Param, ConstStr255Param);
short StopAlert(short, void *);
void SysBeep(short);
void ExitToShell(void);
#endif
