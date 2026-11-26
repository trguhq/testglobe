/* Manual-derived System 7 call recorder contract. NOT MPW SDK or ABI. */
#ifndef TEST_QD_CLASSIC_H
#define TEST_QD_CLASSIC_H
#include "Types.h"
typedef OSType ResType;
typedef long Fixed;
typedef short QDErr;
typedef unsigned char Style;
typedef struct { short ascent, descent, widMax, leading; } FontInfo;
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
typedef Cursor *CursPtr;
typedef CursPtr *CursHandle;
typedef struct { long randSeed; BitMap screenBits; Cursor arrow; Pattern dkGray, ltGray, gray, black, white; GrafPtr thePort; } QDGlobals;
extern QDGlobals qd;
#define noErr 0
#define memFullErr (-108)
#define gestaltQuickdrawVersion 0x71642020UL
#define gestaltProcessorType 0x63707520UL
#define gestaltFPUType 0x66707520UL
#define gestaltSystemVersion 0x73797376UL
#define gestaltAppleEventsAttr 0x65766E74UL
#define gestaltAppleEventsPresent 0
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
#define pmInhibitG2 0x0100
#define pmInhibitC2 0x0200
#define pmInhibitG4 0x0400
#define pmInhibitC4 0x0800
#define pmInhibitG8 0x1000
#define pmInhibitC8 0x2000
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
#define kHighLevelEvent 23
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
#define watchCursor 4
/* Apple Event Manager, Universal Interfaces 2.2 spelling. */
typedef unsigned long DescType;
typedef unsigned long AEKeyword;
typedef unsigned long AEEventClass;
typedef unsigned long AEEventID;
typedef struct { DescType descriptorType; Handle dataHandle; } AEDesc;
typedef AEDesc AppleEvent;
typedef pascal OSErr (*AEEventHandlerProcPtr)(AppleEvent *, AppleEvent *, long);
typedef AEEventHandlerProcPtr AEEventHandlerUPP;
#define NewAEEventHandlerProc(proc) ((AEEventHandlerUPP)(proc))
#define kCoreEventClass 0x61657674UL
#define kAEOpenApplication 0x6F617070UL
#define kAEOpenDocuments 0x6F646F63UL
#define kAEPrintDocuments 0x70646F63UL
#define kAEQuitApplication 0x71756974UL
#define keyMissedKeywordAttr 0x6D697373UL
#define typeWildCard 0x2A2A2A2AUL
#define errAEDescNotFound (-1701)
#define errAEEventNotHandled (-1708)
#define errAEParamMissed (-1715)
/* Memory Manager grow-zone procedure, 68K routine descriptor macro. */
typedef pascal long (*GrowZoneProcPtr)(Size);
typedef GrowZoneProcPtr GrowZoneUPP;
#define NewGrowZoneProc(proc) ((GrowZoneUPP)(proc))

Ptr NewPtr(Size);
void DisposePtr(Ptr);
Handle NewHandle(Size);
void DisposeHandle(Handle);
void HLock(Handle);
void HUnlock(Handle);
Size GetHandleSize(Handle);
Handle Get1Resource(OSType, short);
void ReleaseResource(Handle);
OSErr ResError(void);
OSErr MemError(void);
void MoreMasters(void);
void SetGrowZone(GrowZoneUPP);
void EmptyHandle(Handle);
void ReallocateHandle(Handle, Size);
Size MaxBlock(void);
long SetCurrentA5(void);
long SetA5(long);
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
long Color2Index(const RGBColor *);
void Index2Color(long, RGBColor *);
CursHandle GetCursor(short);
void SetCursor(const Cursor *);
void GetFNum(ConstStr255Param, short *);
Boolean RealFont(short, short);
void GetFontInfo(FontInfo *);
void TextFont(short);
void TextSize(short);
void TextMode(short);
short TextWidth(const void *, short, short);
short CharWidth(short);
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
void AppendResMenu(MenuHandle, ResType);
void GetMenuItemText(MenuHandle, short, Str255);
short OpenDeskAcc(ConstStr255Param);
void InsertMenu(MenuHandle, short);
void DeleteMenu(short);
void DisposeMenu(MenuHandle);
void DrawMenuBar(void);
void ClearMenuBar(void);
void DrawGrowIcon(WindowPtr);
void ValidRect(const Rect *);
RgnHandle GetGrayRgn(void);
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
short NoteAlert(short, void *);
OSErr AEInstallEventHandler(AEEventClass, AEEventID, AEEventHandlerUPP, long, Boolean);
OSErr AEProcessAppleEvent(const EventRecord *);
OSErr AEGetAttributePtr(const AppleEvent *, AEKeyword, DescType, DescType *, void *, Size, Size *);
void SysBeep(short);
void ExitToShell(void);
#endif
