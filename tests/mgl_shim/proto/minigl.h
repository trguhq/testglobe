/* Minimal declarations used by the MiniGL host recorders. Not a native SDK
 * or an ABI layout; the real V29 headers are checked by the VBCC build. */
#ifndef TEST_MINIGL_H
#define TEST_MINIGL_H
#include "../../ogl_record/GL/glut.h"
#include <exec/types.h>
extern void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat);
#define GL_TEXTURE_2D_BINDING GL_TEXTURE_BINDING_2D
#define MINIGLNAME "minigl.library"
#define MINIGL_VERSION 29
#define MINIGL_DISPATCH_ABI_VERSION 5UL
#define MINIGL_BACKEND_FLAG_STUB 1UL
#define MINIGL_BACKEND_FLAG_CLASSIC 2UL
#define MINIGL_BACKEND_FLAG_PISTORM3D 4UL
struct GLcontext_t { int identifier; };
typedef struct GLcontext_t *GLcontext;
struct Window;
struct BitMap;
/* The actual SDK macros carry an explicit context through the dispatch table.
 * The host fixture records these calls without pretending to emulate its ABI. */
extern void mglChooseVertexBufferSize(int);
extern void mglChooseWindowMode(GLboolean);
extern void mglChoosePixelDepth(int);
extern void mglChooseZBufferDepth(int);
extern void mglChooseNumberOfBuffers(int);
extern void *mglCreateContextFromWindow(struct Window *);
extern void *mglCreateContextFromBitMap(struct BitMap *);
extern void mglProhibitMipMapping(GLboolean);
extern void mglDeleteContext(void);
extern void mglSwitchDisplay(void);
extern GLboolean mglResizeContext(GLsizei, GLsizei);

typedef struct MGLDispatchTable {
    ULONG abiVersion, structSize, backendFlags;
    GLcontext *currentContext;
    void (*GLBegin)(void);
    void (*GLEnd)(void);
    void (*GLBindTexture)(void);
    void (*GLClear)(void);
    void (*GLClearColor)(void);
    void (*GLColor4ub)(void);
    void (*GLColor4f)(void);
    void (*GLCullFace)(void);
    void (*GLDeleteTextures)(void);
    void (*GLFlush)(void);
    void (*GLFrontFace)(void);
    void (*GLGenTextures)(void);
    void (*GLGetError)(void);
    void (*GLGetFloatv)(void);
    void (*GLGetIntegerv)(void);
    void (*GLGetString)(void);
    void (*GLIsEnabled)(void);
    void (*GLIsTexture)(void);
    void (*GLLoadIdentity)(void);
    void (*GLMatrixMode)(void);
    void (*GLPopMatrix)(void);
    void (*GLPushMatrix)(void);
    void (*GLRotatef)(void);
    void (*GLShadeModel)(void);
    void (*GLTexCoord2fv)(void);
    void (*GLTexEnvi)(void);
    void (*GLTexImage2D)(void);
    void (*GLTexParameteri)(void);
    void (*GLTranslatef)(void);
    void (*GLVertex3fv)(void);
    void (*GLViewport)(void);
    void (*GLPixelStorei)(void);
    void (*GLUPerspective)(void);
    void (*GLUBuild2DMipmaps)(void);
    void (*GLGenLists)(void);
    void (*GLDeleteLists)(void);
    void (*GLNewList)(void);
    void (*GLEndList)(void);
    void (*GLCallList)(void);
    void (*GLIsList)(void);
    void (*MGLSetState)(void);
    void (*MGLCreateContextFromBitMap)(void);
    void (*MGLDeleteContext)(void);
    void (*MGLSwitchDisplay)(void);
    void (*mglChooseVertexBufferSize)(int);
    void (*mglChooseZBufferDepth)(int);
    void (*mglProhibitMipMapping)(GLboolean);
} MGLDispatchTable;
extern struct Library *MiniGLBase;
extern const MGLDispatchTable *MiniGLDispatch;
extern BOOL MiniGLOpen(void);
extern void MiniGLClose(void);

#endif
