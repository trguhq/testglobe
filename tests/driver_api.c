/* Compile-only public rendering API: expose supported operations precisely. */
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "glob_ext.h"
#include "tex_ext.h"

#if defined(DRV_OGL)
#if !defined(DRV_TRI_COLOR) || !defined(DRV_TRI_RGB) || !defined(DRV_TRI_UV)
#error OpenGL must advertise retained triangle color, RGB and UV callbacks
#endif
#elif defined(DRV_IGL)
#if !defined(DRV_TRI_COLOR) || defined(DRV_TRI_RGB) || !defined(DRV_TRI_UV)
#error IRIS GL must advertise its triangle color and UV callbacks
#endif
#elif defined(DRV_MTX)
#if !defined(DRV_TRI_COLOR) || defined(DRV_TRI_RGB) || defined(DRV_TRI_UV)
#error Matrox must advertise its additional indexed triangle callback
#endif
#else
#if defined(DRV_TRI_COLOR) || defined(DRV_TRI_RGB) || defined(DRV_TRI_UV)
#error This driver does not implement individual triangle callbacks
#endif
#endif

void test_driver_api(void)
{
    (void)sizeof(&drv_render);
    (void)sizeof(&drv_close);
#ifdef DRV_TRI_COLOR
    (void)sizeof(&drv_draw_tri_flat_color);
#endif
#ifdef DRV_TRI_RGB
    (void)sizeof(&drv_draw_tri_flat_rgb);
#endif
#ifdef DRV_TRI_UV
    (void)sizeof(&drv_draw_tri_flat_uv);
#endif
#ifdef TRI
    (void)sizeof(&globe_draw_tris);
#endif
#ifdef DRV_TRI_CAPS
    (void)sizeof(&globe_draw_pole_tris);
#endif
#ifdef TRISTRIP
    (void)sizeof(&globe_draw_tristrips);
#ifdef DRV_COLORMAP
    (void)sizeof(&drv_draw_tristrip_flat_color);
#else
    {
        void (*draw_rgb)(unsigned int, const Vertex *, const Color_RGB *);
        draw_rgb = drv_draw_tristrip_flat_rgb;
        (void)draw_rgb;
    }
#endif
#ifdef TEXTURE
    {
        void (*draw_uv)(unsigned int, const Vertex TG_HUGE *,
                        const Coord TG_HUGE *);
        draw_uv = drv_draw_tristrip_flat_uv;
        (void)draw_uv;
    }
#endif
#endif
#ifdef QUAD
    (void)sizeof(&drv_draw_quad_flat_color);
    (void)sizeof(&globe_draw_quads);
#endif
#ifdef DRV_QUAD_BATCH
    (void)sizeof(&drv_draw_quadbatch_flat_color);
    (void)sizeof(&globe_draw_quadbatches);
#endif
#ifdef TEST_HIDDEN_SYMBOL
    /* The runner expects this compile to fail for unsupported operations. */
    (void)sizeof(&TEST_HIDDEN_SYMBOL);
#endif
}
