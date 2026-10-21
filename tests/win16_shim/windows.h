/* Manual-derived Win16 call-recording declarations, NOT Windows SDK/ABI. */
#ifndef TEST_WINDOWS_H
#define TEST_WINDOWS_H
#include <stddef.h>
#define FAR
#define NEAR
#define PASCAL
#define CALLBACK
#define WINAPI
#ifdef NULL
#undef NULL
#endif
#define NULL 0
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef WORD ATOM;
typedef unsigned short UINT;
typedef int BOOL;
typedef long LONG;
typedef unsigned long DWORD;
typedef unsigned short WPARAM;
typedef long LPARAM;
typedef long LRESULT;
typedef unsigned long COLORREF;
typedef unsigned short HANDLE;
typedef HANDLE HINSTANCE;
typedef HANDLE HWND;
typedef HANDLE HDC;
typedef HANDLE HPALETTE;
typedef HANDLE HBITMAP;
typedef HANDLE HBRUSH;
typedef HANDLE HPEN;
typedef HANDLE HICON;
typedef HANDLE HCURSOR;
typedef HANDLE HGDIOBJ;
typedef HANDLE HGLOBAL;
typedef HANDLE HRSRC;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef void *HUGEPOINTER;
typedef int (*FARPROC)(void);
typedef LONG (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void (*TIMERPROC)(HWND, UINT, UINT, DWORD);
typedef struct { short x, y; } POINT;
typedef POINT *LPPOINT;
typedef const POINT *LPCPOINT;
typedef struct { short left, top, right, bottom; } RECT;
typedef RECT *LPRECT;
typedef struct {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore, fIncUpdate;
    BYTE rgbReserved[16];
} PAINTSTRUCT;
typedef struct {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName, lpszClassName;
} WNDCLASS;
typedef struct {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG;
typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
typedef struct { WORD palVersion, palNumEntries; PALETTEENTRY palPalEntry[1]; } LOGPALETTE;
typedef LOGPALETTE *LPLOGPALETTE;

#define WF_80x87 0x0400UL
#define WF_CPU286 0x0002UL
#define WF_CPU386 0x0004UL
#define WF_CPU486 0x0008UL
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GMEM_FIXED 0
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define PC_RESERVED 0x01
#define PC_NOCOLLAPSE 0x04
#define RC_PALETTE 0x0100
#define HORZRES 8
#define VERTRES 10
#define RASTERCAPS 38
#define BITSPIXEL 12
#define PLANES 14
#define NUMCOLORS 24
#define SIZEPALETTE 104
#define NULL_PEN 8
#define BLACK_BRUSH 4
#define WHITE_BRUSH 0
#define SYSTEM_FONT 13
#define SRCCOPY 0x00CC0020UL
#define BLACKNESS 0x00000042UL
#define DT_LEFT 0x0000
#define DT_TOP 0x0000
#define DT_WORDBREAK 0x0010
#define DT_CALCRECT 0x0400
#define DT_NOPREFIX 0x0800
#define TRANSPARENT 1
#define OPAQUE 2
#define WINDING 2
#define ALTERNATE 1
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_OWNDC 0x0020
#define WS_OVERLAPPEDWINDOW 0x00CF0000UL
#define WS_OVERLAPPED 0
#define WS_CAPTION 0x00C00000UL
#define WS_SYSMENU 0x00080000UL
#define WS_MINIMIZEBOX 0x00020000UL
#define WS_MAXIMIZEBOX 0x00010000UL
#define WS_THICKFRAME 0x00040000UL
#define WS_VISIBLE 0x10000000UL
#define CW_USEDEFAULT (-32768)
#define SW_SHOW 5
#define IDC_ARROW ((LPCSTR)32512UL)
#define IDI_APPLICATION ((LPCSTR)32512UL)
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_QUERYNEWPALETTE 0x030F
#define WM_PALETTECHANGED 0x0311
#define WM_KEYDOWN 0x0100
#define WM_CHAR 0x0102
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_ACTIVATEAPP 0x001C
#define WM_CANCELMODE 0x001F
#define SIZE_MINIMIZED 1
#define WA_INACTIVE 0
#define VK_ESCAPE 0x1B
#define MK_LBUTTON 0x0001
#define PM_REMOVE 0x0001
#define MB_OK 0
#define MB_ICONSTOP 0x0010
#define MB_ICONEXCLAMATION 0x0030
#define MB_TASKMODAL 0x2000
#define LOWORD(value) ((WORD)((DWORD)(value) & 0xffffUL))
#define HIWORD(value) ((WORD)(((DWORD)(value) >> 16) & 0xffffUL))
#define RGB(r,g,b) ((COLORREF)((BYTE)(r) | ((WORD)(BYTE)(g) << 8) | ((DWORD)(BYTE)(b) << 16)))
#define MAKEINTRESOURCE(i) ((LPCSTR)(unsigned long)(WORD)(i))
#define RT_RCDATA MAKEINTRESOURCE(10)
#define UnlockResource(resource) GlobalUnlock(resource)
#define PALETTEINDEX(i) (0x01000000UL | (DWORD)(WORD)(i))

DWORD GetWinFlags(void);
DWORD GetTickCount(void);
HGLOBAL GlobalAlloc(UINT, DWORD);
LPVOID GlobalLock(HGLOBAL);
BOOL GlobalUnlock(HGLOBAL);
HGLOBAL GlobalFree(HGLOBAL);
DWORD GlobalHandle(UINT);
DWORD GlobalSize(HGLOBAL);
HRSRC FindResource(HINSTANCE, LPCSTR, LPCSTR);
DWORD SizeofResource(HINSTANCE, HRSRC);
HGLOBAL LoadResource(HINSTANCE, HRSRC);
LPSTR LockResource(HGLOBAL);
BOOL FreeResource(HGLOBAL);
FARPROC MakeProcInstance(FARPROC, HINSTANCE);
void FreeProcInstance(FARPROC);
ATOM RegisterClass(const WNDCLASS *);
BOOL UnregisterClass(LPCSTR, HINSTANCE);
HWND CreateWindow(LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HANDLE, HINSTANCE, LPVOID);
BOOL DestroyWindow(HWND);
BOOL ShowWindow(HWND, int);
void UpdateWindow(HWND);
LONG DefWindowProc(HWND, UINT, WPARAM, LPARAM);
BOOL GetMessage(MSG *, HWND, UINT, UINT);
BOOL PeekMessage(MSG *, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG *);
LONG DispatchMessage(const MSG *);
void PostQuitMessage(int);
void InvalidateRect(HWND, const RECT *, BOOL);
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
HDC BeginPaint(HWND, PAINTSTRUCT *);
void EndPaint(HWND, const PAINTSTRUCT *);
void GetClientRect(HWND, RECT *);
void AdjustWindowRect(RECT *, DWORD, BOOL);
HCURSOR LoadCursor(HINSTANCE, LPCSTR);
HICON LoadIcon(HINSTANCE, LPCSTR);
HDC CreateCompatibleDC(HDC);
BOOL DeleteDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC, int, int);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL DeleteObject(HGDIOBJ);
HGDIOBJ GetStockObject(int);
int GetDeviceCaps(HDC, int);
HPALETTE CreatePalette(const LOGPALETTE *);
HPALETTE SelectPalette(HDC, HPALETTE, BOOL);
UINT RealizePalette(HDC);
BOOL UnrealizeObject(HGDIOBJ);
HBRUSH CreateSolidBrush(COLORREF);
BOOL Polygon(HDC, const POINT *, int);
BOOL PolyPolygon(HDC, const POINT *, int *, int);
BOOL PatBlt(HDC, int, int, int, int, DWORD);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
int FillRect(HDC, const RECT *, HBRUSH);
int SetPolyFillMode(HDC, int);
int SetBkMode(HDC, int);
COLORREF SetTextColor(HDC, COLORREF);
COLORREF SetBkColor(HDC, COLORREF);
BOOL TextOut(HDC, int, int, LPCSTR, int);
int DrawText(HDC, LPCSTR, int, RECT *, UINT);
HWND SetCapture(HWND);
void ReleaseCapture(void);
UINT SetTimer(HWND, UINT, UINT, TIMERPROC);
BOOL KillTimer(HWND, UINT);
int MessageBox(HWND, LPCSTR, LPCSTR, UINT);

#endif
