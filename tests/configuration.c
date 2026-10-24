/* Compile-only selection checks; no host platform SDK is needed. */
#include "testglob.h"
#include "drv.h"
#include "types.h"

#ifdef TEST_DRIVER_OGL
#if !defined(DRV_OGL) || !defined(FLAT) || defined(SEG86) || defined(SEG68)
#error OpenGL selection must use the flat architecture
#endif
#endif
#ifdef TEST_DRIVER_XGL
#if !defined(DRV_XGL) || !defined(FLAT) || defined(SEG86) || defined(SEG68)
#error XGL selection must use the flat architecture
#endif
#endif
#ifdef TEST_DRIVER_IGL
#if !defined(DRV_IGL) || !defined(FLAT) || defined(SEG86) || defined(SEG68)
#error IRIS GL selection must use the flat architecture
#endif
#endif
#ifdef TEST_DRIVER_QD
#if !defined(DRV_QD) || !defined(SEG68) || defined(FLAT) || defined(SEG86)
#error QuickDraw selection must use the segmented 68000 architecture
#endif
#endif
#ifdef TEST_DRIVER_GDI
#if !defined(DRV_GDI) || !defined(SEG86) || defined(FLAT) || defined(SEG68)
#error GDI selection must use the segmented 8086 architecture
#endif
#endif
#ifdef TEST_DRIVER_MTX
#if !defined(DRV_MTX) || !defined(SEG86) || defined(FLAT) || defined(SEG68)
#error Matrox selection must use the segmented 8086 architecture
#endif
#endif

#if defined(TEST_DRIVER_OGL) || defined(TEST_DRIVER_IGL) || defined(TEST_DRIVER_XGL)
#ifdef DRV_MAX_RADIUS
#error Native floating-point drivers must not inherit a fixed-point radius limit
#endif
#endif
#if defined(TEST_DRIVER_GDI) || defined(TEST_DRIVER_QD) || defined(TEST_DRIVER_MTX)
#if !defined(DRV_MAX_RADIUS) || DRV_MAX_RADIUS != 1000L
#error Bounded drivers must retain their validated radius limit
#endif
#endif
#if defined(TEST_DRIVER_GDI) || defined(TEST_DRIVER_QD)
#ifndef DRV_NO_CONSOLE
#error Native GUI drivers must suppress console diagnostics
#endif
#else
#ifdef DRV_NO_CONSOLE
#error Console drivers must retain mesh diagnostics
#endif
#endif

void test_configuration(void)
{
#ifdef TEST_DRIVER_QD
    Int native_coordinate = 32767L + 12L;
    (void)native_coordinate;
#endif
}
