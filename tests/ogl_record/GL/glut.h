/* Public OpenGL/GLUT declarations for host call recording.
 * This fixture does not provide GL rendering, a visual, or an ABI test. */
#ifndef TEST_OGL_GLUT_H
#define TEST_OGL_GLUT_H

#include <stdio.h>
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_STACK_OVERFLOW 0x0503
#define GL_STACK_UNDERFLOW 0x0504
#define GL_OUT_OF_MEMORY 0x0505
#define GLU_OUT_OF_MEMORY 100902
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_COMPILE 0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_LINEAR 0x2601
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_CULL_FACE 0x0B44
#define GL_CCW 0x0901
#define GL_BACK 0x0405
#define GL_COLOR_INDEX 0x1900
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_RED_BITS 0x0D52
#define GL_GREEN_BITS 0x0D53
#define GL_BLUE_BITS 0x0D54
#define GL_ALPHA_BITS 0x0D55
#define GL_INDEX_BITS 0x0D51
#define GL_RGBA_MODE 0x0C31
#define GL_DOUBLEBUFFER 0x0C32
#define GL_DEPTH_TEST 0x0B71
#define GL_LEQUAL 0x0203
#define GL_FLAT 0x1D00
#define GL_INDEX_WRITEMASK 0x0C21
#define GL_DEPTH_BITS 0x0D56
#define GL_VIEWPORT 0x0BA2
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_MAP_COLOR 0x0D10
#define GL_ZOOM_X 0x0D16
#define GL_ZOOM_Y 0x0D17
#define GL_READ_BUFFER 0x0C02
#define GL_DRAW_BUFFER 0x0C01
#define GL_CURRENT_RASTER_POSITION_VALID 0x0B08
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_FRONT 0x0404
#define GLUT_RGB 0x0000
#define GLUT_RGBA 0x0000
#define GLUT_INDEX 0x0001
#define GLUT_SINGLE 0x0000
#define GLUT_DOUBLE 0x0002
#define GLUT_ALPHA 0x0008
#define GLUT_DEPTH 0x0010
#define GLUT_DISPLAY_MODE_POSSIBLE 0x0190
#define GLUT_WINDOW_BUFFER_SIZE 0x0068
#define GLUT_WINDOW_ALPHA_SIZE 0x006E
#define GLUT_WINDOW_RED_SIZE 0x006B
#define GLUT_WINDOW_GREEN_SIZE 0x006C
#define GLUT_WINDOW_BLUE_SIZE 0x006D
#define GLUT_WINDOW_RGBA 0x0074
#define GLUT_WINDOW_DOUBLEBUFFER 0x0073
#define GLUT_WINDOW_COLORMAP_SIZE 0x0077
#define GLUT_LEFT_BUTTON 0x0000
#define GLUT_RIGHT_BUTTON 0x0002
#define GLUT_DOWN 0x0000
#define GLUT_UP 0x0001
#define GLUT_WINDOW_DEPTH_SIZE 0x006A
#define GL_INDEX_SHIFT 0x0D12
#define GL_INDEX_OFFSET 0x0D13
#define GL_MATRIX_MODE 0x0BA0
#define GL_ENABLE_BIT 0x00002000
#define GL_PIXEL_MODE_BIT 0x00000020
#define GL_CURRENT_BIT 0x00000001
#define GL_CLIENT_PIXEL_STORE_BIT 0x00000001
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_REPLACE 0x1E01
#define GLUT_API_VERSION 4
#define GLUT_BITMAP_HELVETICA_12 ((void *)1)
extern void glutInit(int *argc,char **argv);
extern void glutInitDisplayMode(unsigned int mode);
extern void glutInitDisplayString(const char *string);
extern int glutGet(GLenum query);
extern void glutInitWindowSize(int width,int height);
extern void glutInitWindowPosition(int x,int y);
extern int glutCreateWindow(const char *title);
extern void glutDestroyWindow(int window);
extern void glutSetWindow(int window);
extern int glutGetWindow(void);
extern void glutHideWindow(void);
extern void glutShowWindow(void);
extern void glutSetWindowTitle(const char *title);
extern void glutPositionWindow(int x,int y);
extern void glutReshapeWindow(int width,int height);
extern void glutDisplayFunc(void (*callback)(void));
extern void glutReshapeFunc(void (*callback)(int,int));
extern void glutMouseFunc(void (*callback)(int,int,int,int));
extern void glutMotionFunc(void (*callback)(int,int));
extern void glutPassiveMotionFunc(void (*callback)(int,int));
extern void glutKeyboardFunc(void (*callback)(unsigned char,int,int));
extern void glutPostRedisplay(void);
extern void glutSwapBuffers(void);
extern void glutMainLoop(void);
extern void glutBitmapCharacter(void *font,int character);
extern void glutSetColor(int index,GLfloat red,GLfloat green,GLfloat blue);
extern GLenum glGetError(void);
extern void glGetIntegerv(GLenum query,GLint *value);
extern void glGetBooleanv(GLenum query,GLboolean *value);
extern GLboolean glIsEnabled(GLenum capability);
extern void glPixelTransferi(GLenum parameter,GLint value);
extern void glPushAttrib(GLbitfield mask);
extern void glPopAttrib(void);
extern void glPushClientAttrib(GLbitfield mask);
extern void glPopClientAttrib(void);
extern void glTexEnvi(GLenum target,GLenum parameter,GLint value);
extern void glGetFloatv(GLenum query,GLfloat *value);
extern void glClear(GLbitfield mask);
extern void glClearColor(GLclampf red,GLclampf green,GLclampf blue,GLclampf alpha);
extern void glClearIndex(GLfloat index);
extern void glEnable(GLenum capability);
extern void glDisable(GLenum capability);
extern void glFrontFace(GLenum mode);
extern void glCullFace(GLenum mode);
extern void glMatrixMode(GLenum mode);
extern void glLoadIdentity(void);
extern void glPushMatrix(void);
extern void glPopMatrix(void);
extern void glTranslatef(GLfloat x,GLfloat y,GLfloat z);
extern void glRotatef(GLfloat angle,GLfloat x,GLfloat y,GLfloat z);
extern void glViewport(GLint x,GLint y,GLsizei width,GLsizei height);
extern GLuint glGenLists(GLsizei count);
extern void glNewList(GLuint list,GLenum mode);
extern void glEndList(void);
extern void glCallList(GLuint list);
extern void glDeleteLists(GLuint list,GLsizei count);
extern void glBegin(GLenum mode);
extern void glEnd(void);
extern void glColor3ub(GLubyte red,GLubyte green,GLubyte blue);
extern void glColor3f(GLfloat red,GLfloat green,GLfloat blue);
extern void glIndexub(GLubyte index);
extern void glIndexi(GLint index);
extern void glIndexf(GLfloat index);
extern void glVertex3fv(const GLfloat *vertex);
extern void glTexCoord2fv(const GLfloat *coordinate);
extern void glRasterPos2i(GLint x,GLint y);
extern void glRasterPos2f(GLfloat x,GLfloat y);
extern void glPixelStorei(GLenum parameter,GLint value);
extern void glPixelZoom(GLfloat x,GLfloat y);
extern void glDrawPixels(GLsizei width,GLsizei height,GLenum format,GLenum type,const GLvoid *pixels);
extern void glGenTextures(GLsizei count,GLuint *textures);
extern void glDeleteTextures(GLsizei count,const GLuint *textures);
extern void glBindTexture(GLenum target,GLuint texture);
extern void glTexParameteri(GLenum target,GLenum parameter,GLint value);
extern void glTexImage2D(GLenum target,GLint level,GLint internal_format,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const GLvoid *pixels);
extern void glGenerateMipmap(GLenum target);
extern void glFlush(void);
extern void glShadeModel(GLenum mode);
extern void glDepthFunc(GLenum function);
extern void gluPerspective(GLdouble fov,GLdouble aspect,GLdouble near_plane,GLdouble far_plane);
extern void gluOrtho2D(GLdouble left,GLdouble right,GLdouble bottom,GLdouble top);
extern GLint gluBuild2DMipmaps(GLenum target,GLint components,GLsizei width,GLsizei height,GLenum format,GLenum type,const GLvoid *pixels);
#endif
