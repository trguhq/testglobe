/* Optional FreeGLUT loader declarations; GL/glut.h remains GL 1.1-only. */
#ifndef TEST_OGL_FREEGLUT_EXT_H
#define TEST_OGL_FREEGLUT_EXT_H
typedef void (*GLUTproc)(void);
extern GLUTproc glutGetProcAddress(const char *name);
#endif
