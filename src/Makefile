# Common make syntax for Solaris/System V make and Microsoft C 7 NMAKE.
# Keep this file in step with ../Makefile; only SRC and XGX_ROOT differ.
# Binaries and objects are written in the directory where make is run.

SRC = .

# Portable Unix cc defaults, including older Solaris math/OpenGL libraries.
CC = cc
CPPFLAGS =
CFLAGS = -O
SYS_FLAGS = -DNO_SINF -DNO_GENMIPMAP
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu
OGL_DEFINES = -DFLAT -DDRV_OGL -DDRV
OGL_OBJECTS = drv.o drv_ogl.o err.o globe.o main.o texture.o
OGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_ogl.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/texture.h $(SRC)/tex_ext.h \
    $(SRC)/e_cmap.h $(SRC)/e_32.h $(SRC)/e_64.h $(SRC)/e_128.h \
    $(SRC)/e_256.h $(SRC)/e_512.h

# Sun XGL 3.x / 32-bit SPARC on Solaris 2.6. Select native SDK locations
# on the make command line if they differ from the Sun installation defaults.
# x_ objects keep this driver's TRISTRIP layout separate from OpenGL's TRI.
XGLHOME = /opt/SUNWits/Graphics-sw/xgl
OPENWINHOME = /usr/openwin
XGL_CPPFLAGS = -I$(XGLHOME)/include -I$(OPENWINHOME)/include
XGL_CFLAGS = $(CFLAGS)
XGL_SYS_FLAGS = -DNO_SINF
XGL_DEFINES = -DFLAT -DDRV_XGL -DDRV
XGL_LDFLAGS = -L$(XGLHOME)/lib -L$(OPENWINHOME)/lib \
    -R$(XGLHOME)/lib:$(OPENWINHOME)/lib -Bdynamic \
    -lxgl -lX11 -ldga -ldl -lm
XGL_OBJECTS = x_drv.o x_xgl.o x_err.o x_globe.o x_main.o x_tex.o
XGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_xgl.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/texture.h $(SRC)/tex_ext.h \
    $(SRC)/e_cmap.h $(SRC)/e_32.h $(SRC)/e_64.h $(SRC)/e_128.h \
    $(SRC)/e_256.h $(SRC)/e_512.h

# Microsoft C 7, huge model, with native DOS paths and 8.3 file names.
MTX_CC = cl
MTX_CFLAGS = -AH -O -W3
MTX_DEFINES = -DSEG86 -DDRV_MTX -DDRV -DNO_SINF
XGX_ROOT = ..
MTX_CPPFLAGS = -I$(XGX_ROOT)
MTX_LIB = $(XGX_ROOT)\libsv5\lscl.lib
MTX_LDFLAGS = -link /STACK:16384
MTX_OBJECTS = drv.obj drv_mtx.obj err.obj globe.obj main.obj
MTX_HEADERS = $(SRC)\testglob.h $(SRC)\types.h $(SRC)\drv.h \
    $(SRC)\drv_ext.h $(SRC)\drv_mtx.h $(SRC)\err_ext.h \
    $(SRC)\globe.h $(SRC)\glob_ext.h $(SRC)\e_cmap.h \
    $(SRC)\e_32.h $(SRC)\e_64.h \
    $(XGX_ROOT)\XGX1\HEADERS\DEF.H \
    $(XGX_ROOT)\XGX1\HEADERS\LIBFN.H \
    $(XGX_ROOT)\XGX1\HEADERS\IOLIB.H

# Native 16-bit Windows 3.1, Microsoft C 7 and the Windows 3.1 SDK.
# /FPi uses a coprocessor when present and emulates otherwise. Keep /Gw
# for Windows prologs and /AH for normalized cross-segment mesh pointers.
# GDI objects and output are separate from the Matrox DOS program.
GDI_CC = cl
GDI_LINK = link
GDI_RC = rc
GDI_CFLAGS = -AH -G2 -Gw -Zp1 -FPi -O -W3
GDI_DEFINES = -DSEG86 -DDRV_GDI -DDRV -D_WINDOWS -DNO_SINF
GDI_CPPFLAGS =
GDI_LIBS = llibcew.lib libw.lib
GDI_LDFLAGS = /NOD /ALIGN:16
GDI_RCFLAGS = -t
GDI_OBJECTS = g_drv.obj g_gdi.obj g_err.obj g_globe.obj g_main.obj
GDI_HEADERS = $(SRC)\testglob.h $(SRC)\types.h $(SRC)\drv.h \
    $(SRC)\drv_ext.h $(SRC)\drv_gdi.h $(SRC)\err_ext.h \
    $(SRC)\globe.h $(SRC)\glob_ext.h $(SRC)\e_cmap.h \
    $(SRC)\e_32.h $(SRC)\e_64.h

# Unix default; with DOS NMAKE use: nmake clean RM=del
RM = rm -f

# The first ordinary target is the portable way to choose the default.
opengl: testglobe

testglobe: $(OGL_OBJECTS) FORCE
	$(CC) -o testglobe $(OGL_OBJECTS) $(LDFLAGS)

drv.o: $(SRC)/drv.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/drv.c

drv_ogl.o: $(SRC)/drv_ogl.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/drv_ogl.c

err.o: $(SRC)/err.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/err.c

globe.o: $(SRC)/globe.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/globe.c

main.o: $(SRC)/main.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/main.c

texture.o: $(SRC)/texture.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/texture.c

# Both Unix backends intentionally write testglobe. Always relink the
# selected backend so switching from "xgl" to "opengl" cannot keep the other
# driver's executable merely because its timestamp is newer than the objects.
FORCE:

xgl: $(XGL_OBJECTS) FORCE
	$(CC) -o testglobe $(XGL_OBJECTS) $(XGL_LDFLAGS)

x_drv.o: $(SRC)/drv.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c $(SRC)/drv.c -o x_drv.o

x_xgl.o: $(SRC)/drv_xgl.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c $(SRC)/drv_xgl.c -o x_xgl.o

x_err.o: $(SRC)/err.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c $(SRC)/err.c -o x_err.o

x_globe.o: $(SRC)/globe.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c $(SRC)/globe.c -o x_globe.o

x_main.o: $(SRC)/main.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c $(SRC)/main.c -o x_main.o

x_tex.o: $(SRC)/texture.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c $(SRC)/texture.c -o x_tex.o

matrox: testglob.exe

# Each echo is short enough for COMMAND.COM. Response files also keep the
# compiler and linker invocations below DOS's 127-character command limit.
testglob.exe: $(MTX_OBJECTS) $(MTX_LIB)
	@echo -AH -Fetestglob.exe > link.rsp
	@echo $(MTX_OBJECTS) >> link.rsp
	@echo $(MTX_LIB) $(MTX_LDFLAGS) >> link.rsp
	$(MTX_CC) @link.rsp

drv.obj: $(SRC)\drv.c $(MTX_HEADERS)
	@echo $(MTX_DEFINES) $(MTX_CFLAGS) > drv.rsp
	@echo $(MTX_CPPFLAGS) -c >> drv.rsp
	@echo -Fodrv.obj $(SRC)\drv.c >> drv.rsp
	$(MTX_CC) @drv.rsp

drv_mtx.obj: $(SRC)\drv_mtx.c $(MTX_HEADERS)
	@echo $(MTX_DEFINES) $(MTX_CFLAGS) > drv_mtx.rsp
	@echo $(MTX_CPPFLAGS) -c >> drv_mtx.rsp
	@echo -Fodrv_mtx.obj $(SRC)\drv_mtx.c >> drv_mtx.rsp
	$(MTX_CC) @drv_mtx.rsp

err.obj: $(SRC)\err.c $(MTX_HEADERS)
	@echo $(MTX_DEFINES) $(MTX_CFLAGS) > err.rsp
	@echo $(MTX_CPPFLAGS) -c >> err.rsp
	@echo -Foerr.obj $(SRC)\err.c >> err.rsp
	$(MTX_CC) @err.rsp

globe.obj: $(SRC)\globe.c $(MTX_HEADERS)
	@echo $(MTX_DEFINES) $(MTX_CFLAGS) > globe.rsp
	@echo $(MTX_CPPFLAGS) -c >> globe.rsp
	@echo -Foglobe.obj $(SRC)\globe.c >> globe.rsp
	$(MTX_CC) @globe.rsp

main.obj: $(SRC)\main.c $(MTX_HEADERS)
	@echo $(MTX_DEFINES) $(MTX_CFLAGS) > main.rsp
	@echo $(MTX_CPPFLAGS) -c >> main.rsp
	@echo -Fomain.obj $(SRC)\main.c >> main.rsp
	$(MTX_CC) @main.rsp

gdi: testgdi.exe

# Use the 16-bit linker, not the compiler's DOS default startup libraries.
# RC marks the module for Windows 3.1 even though it has no resources.
testgdi.exe: $(GDI_OBJECTS) $(SRC)\gdi.def
	@echo $(GDI_OBJECTS) $(GDI_LDFLAGS) > g_link.rsp
	@echo testgdi.exe >> g_link.rsp
	@echo NUL >> g_link.rsp
	@echo $(GDI_LIBS) >> g_link.rsp
	@echo $(SRC)\gdi.def; >> g_link.rsp
	$(GDI_LINK) @g_link.rsp
	$(GDI_RC) $(GDI_RCFLAGS) testgdi.exe

g_drv.obj: $(SRC)\drv.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_drv.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_drv.rsp
	@echo -Fog_drv.obj $(SRC)\drv.c >> g_drv.rsp
	$(GDI_CC) @g_drv.rsp

g_gdi.obj: $(SRC)\drv_gdi.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_gdi.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_gdi.rsp
	@echo -Fog_gdi.obj $(SRC)\drv_gdi.c >> g_gdi.rsp
	$(GDI_CC) @g_gdi.rsp

g_err.obj: $(SRC)\err.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_err.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_err.rsp
	@echo -Fog_err.obj $(SRC)\err.c >> g_err.rsp
	$(GDI_CC) @g_err.rsp

g_globe.obj: $(SRC)\globe.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_globe.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_globe.rsp
	@echo -Fog_globe.obj $(SRC)\globe.c >> g_globe.rsp
	$(GDI_CC) @g_globe.rsp

g_main.obj: $(SRC)\main.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_main.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_main.rsp
	@echo -Fog_main.obj $(SRC)\main.c >> g_main.rsp
	$(GDI_CC) @g_main.rsp

# Separate targets can clean one backend; "clean" removes all.
# One file per command also works with the original DOS DEL command.
# Ignore missing-file errors from DEL (Unix rm -f is already silent).
clean: clean-opengl clean-xgl clean-matrox clean-gdi

clean-opengl:
	-$(RM) drv.o
	-$(RM) drv_ogl.o
	-$(RM) err.o
	-$(RM) globe.o
	-$(RM) main.o
	-$(RM) texture.o
	-$(RM) testglobe

clean-xgl:
	-$(RM) x_drv.o
	-$(RM) x_xgl.o
	-$(RM) x_err.o
	-$(RM) x_globe.o
	-$(RM) x_main.o
	-$(RM) x_tex.o
	-$(RM) testglobe

clean-matrox:
	-$(RM) drv.obj
	-$(RM) drv_mtx.obj
	-$(RM) err.obj
	-$(RM) globe.obj
	-$(RM) main.obj
	-$(RM) drv.rsp
	-$(RM) drv_mtx.rsp
	-$(RM) err.rsp
	-$(RM) globe.rsp
	-$(RM) main.rsp
	-$(RM) link.rsp
	-$(RM) testglob.exe

clean-gdi:
	-$(RM) g_drv.obj
	-$(RM) g_gdi.obj
	-$(RM) g_err.obj
	-$(RM) g_globe.obj
	-$(RM) g_main.obj
	-$(RM) g_drv.rsp
	-$(RM) g_gdi.rsp
	-$(RM) g_err.rsp
	-$(RM) g_globe.rsp
	-$(RM) g_main.rsp
	-$(RM) g_link.rsp
	-$(RM) testgdi.exe
