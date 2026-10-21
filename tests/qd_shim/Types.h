/* Manual-derived Classic Macintosh call declarations, NOT MPW SDK/ABI. */
#ifndef TEST_QD_TYPES_H
#define TEST_QD_TYPES_H
#include <stddef.h>
typedef unsigned char Boolean;
typedef unsigned char Byte;
typedef signed char SignedByte;
typedef short OSErr;
typedef long Size;
typedef char *Ptr;
typedef Ptr *Handle;
typedef unsigned long OSType;
typedef unsigned char Str255[256];
typedef const unsigned char *ConstStr255Param;
#ifndef pascal
#define pascal
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#ifndef nil
#define nil NULL
#endif
#endif
