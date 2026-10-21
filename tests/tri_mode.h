/* Keep the original TRI traversal covered after OpenGL selects strips. */
#ifndef tests_tri_mode_h
#define tests_tri_mode_h

#include "drv_ogl.h"
#undef TRISTRIP
#undef DRV_TRI_CAPS
#define TRI

#endif /* tests_tri_mode_h */
