# Common make syntax for IRIX/Solaris System V make and Microsoft C 7 NMAKE.
# Keep this file in step with ../Makefile; directory defaults differ.
# Binaries and objects are written in the directory where make is run.
# Driver headers select rendering callbacks; drv.c contains shared helpers.

SRC = .

# Portable Unix cc defaults, including older Solaris math/OpenGL libraries.
CC = cc
CPPFLAGS =
CFLAGS = -O
SYS_FLAGS = -DNO_SINF -DNO_GENMIPMAP
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu
# drv_ogl.h selects latitude strips with separately batched pole triangles.
OGL_DEFINES = -DFLAT -DDRV_OGL -DDRV
OGL_OBJECTS = drv.o drv_ogl.o err.o globe.o main.o texture.o texsoft.o
OGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_ogl.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/texture.h $(SRC)/tex_ext.h $(SRC)/texsoft.h \
    $(SRC)/e_cmap.h $(SRC)/e_32.h $(SRC)/e_64.h $(SRC)/e_128.h \
    $(SRC)/e_256.h $(SRC)/e_512.h

# Native Solaris OpenGL/GLUT with the installed OpenWindows headers/libraries.
# s_ objects keep Solaris compiler and linker options separate from other builds.
OPENWINHOME = /usr/openwin
SOLARIS_CC = $(CC)
SOLARIS_CPPFLAGS = -I$(OPENWINHOME)/include
SOLARIS_CFLAGS = $(CFLAGS)
SOLARIS_SYS_FLAGS = -DNO_SINF -DNO_GENMIPMAP
SOLARIS_DEFINES = $(OGL_DEFINES)
SOLARIS_LDFLAGS = -L$(OPENWINHOME)/lib -R$(OPENWINHOME)/lib \
    -lglut -lGL -lGLU -lX11 -lXmu -lm
SOLARIS_OBJECTS = s_drv.o s_ogl.o s_err.o s_globe.o s_main.o s_tex.o s_soft.o

# Native macOS OpenGL/GLUT from the selected Xcode command line tools.
# Apple objects stay separate from Unix GL/X11 objects. Apple's legacy
# OpenGL context uses GLU mipmaps rather than core glGenerateMipmap.
APPLE_CC = $(CC)
APPLE_CPPFLAGS =
APPLE_CFLAGS = $(CFLAGS)
APPLE_SYS_FLAGS = -DNO_GENMIPMAP -DGLUT_SILENCE_DEPRECATION
APPLE_DEFINES = $(OGL_DEFINES)
APPLE_LDFLAGS = -framework GLUT -framework OpenGL -framework Cocoa
APPLE_OBJECTS = a_drv.o a_ogl.o a_err.o a_globe.o a_main.o a_tex.o a_soft.o

# Sun XGL 3.x / 32-bit SPARC on Solaris 2.6. Select native SDK locations
# on the make command line if they differ from the Sun installation defaults.
# x_ objects keep XGL's RGB strip packets separate from OpenGL driver objects.
XGLHOME = /opt/SUNWits/Graphics-sw/xgl
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

# Native IRIS GL on 32-bit MIPS IRIX 4.0. These shared-library names follow
# the IRIS GL Programming Guide; override for the installed SGI toolchain.
# i_ objects keep this driver separate from both other Unix backends.
IGL_CC = $(CC)
IGL_CPPFLAGS =
IGL_CFLAGS = $(CFLAGS)
IGL_SYS_FLAGS = -DNO_SINF
IGL_DEFINES = -DFLAT -DDRV_IGL -DDRV
IGL_LDFLAGS = -lc_s -lgl_s -lm
IGL_OBJECTS = i_drv.o i_igl.o i_err.o i_globe.o i_main.o i_tex.o i_soft.o
IGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_igl.h $(SRC)/texsoft.h $(SRC)/err_ext.h \
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
# GDI objects are separate from the Matrox DOS program.
# drv_gdi.h selects QUAD/DRV_QUAD_BATCH; globe_draw_quadbatches submits packets.
GDI_CC = cl
GDI_LINK = link
GDI_RC = rc
GDI_CFLAGS = -AH -G2 -Gw -Zp1 -FPi -O -W3
GDI_DEFINES = -DSEG86 -DDRV_GDI -DDRV -D_WINDOWS -DNO_SINF
GDI_CPPFLAGS =
GDI_LIBS = llibcew.lib libw.lib
GDI_LDFLAGS = /NOD /ALIGN:16
GDI_RCFLAGS = -t
GDI_DATA = ..\data
GDI_OBJECTS = g_drv.obj g_gdi.obj g_err.obj g_globe.obj g_main.obj g_soft.obj
GDI_HEADERS = $(SRC)\testglob.h $(SRC)\types.h $(SRC)\drv.h \
    $(SRC)\drv_ext.h $(SRC)\drv_gdi.h $(SRC)\err_ext.h \
    $(SRC)\globe.h $(SRC)\glob_ext.h $(SRC)\e_cmap.h \
    $(SRC)\earthres.h $(SRC)\soft3d.h
GDI_RESOURCE_DATA = $(GDI_DATA)\e32.bin $(GDI_DATA)\e64.bin \
    $(GDI_DATA)\e128.bin $(GDI_DATA)\e256_0.bin $(GDI_DATA)\e256_1.bin \
    $(GDI_DATA)\e512_0.bin $(GDI_DATA)\e512_1.bin $(GDI_DATA)\e512_2.bin \
    $(GDI_DATA)\e512_3.bin $(GDI_DATA)\e512_4.bin $(GDI_DATA)\e512_5.bin \
    $(GDI_DATA)\e512_6.bin $(GDI_DATA)\e512_7.bin

# MPW Make has its own grammar. mpw-make is an external bridge that runs the
# native Make tool and executes its generated MPW commands; override MPW_EXEC
# for a configured equivalent. Native MPW users use Makefile.mpw directly.
# drv_qd.h selects QUAD/DRV_QUAD_BATCH; globe_draw_quadbatches submits packets.
MPW_EXEC = mpw-make
QD_MAKEFILE = ../Makefile.mpw
QD_SOURCE = :
QD_DEFINES = -d SEG68 -d DRV_QD -d DRV -d NO_SINF

# Unix default; with DOS NMAKE use: nmake clean RM=del
RM = rm -f

# The first ordinary target is the portable way to choose the default.
opengl: testglobe-opengl

testglobe-opengl: $(OGL_OBJECTS) FORCE
	$(CC) -o testglobe-opengl $(OGL_OBJECTS) $(LDFLAGS)

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

texsoft.o: $(SRC)/texsoft.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c $(SRC)/texsoft.c

# This target uses native Solaris OpenWindows paths for the OpenGL binary.
# FORCE relinks it when switching among the OpenGL platform targets.
solaris-opengl: $(SOLARIS_OBJECTS) FORCE
	$(SOLARIS_CC) -o testglobe-opengl $(SOLARIS_OBJECTS) $(SOLARIS_LDFLAGS)

s_drv.o: $(SRC)/drv.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/drv.c -o s_drv.o

s_ogl.o: $(SRC)/drv_ogl.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/drv_ogl.c -o s_ogl.o

s_err.o: $(SRC)/err.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/err.c -o s_err.o

s_globe.o: $(SRC)/globe.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/globe.c -o s_globe.o

s_main.o: $(SRC)/main.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/main.c -o s_main.o

s_tex.o: $(SRC)/texture.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/texture.c -o s_tex.o

s_soft.o: $(SRC)/texsoft.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c $(SRC)/texsoft.c -o s_soft.o

# This target produces the same OpenGL binary with native Apple frameworks.
# FORCE relinks it when switching between opengl and apple-opengl.
apple-opengl: $(APPLE_OBJECTS) FORCE
	$(APPLE_CC) -o testglobe-opengl $(APPLE_OBJECTS) $(APPLE_LDFLAGS)

a_drv.o: $(SRC)/drv.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/drv.c -o a_drv.o

a_ogl.o: $(SRC)/drv_ogl.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/drv_ogl.c -o a_ogl.o

a_err.o: $(SRC)/err.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/err.c -o a_err.o

a_globe.o: $(SRC)/globe.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/globe.c -o a_globe.o

a_main.o: $(SRC)/main.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/main.c -o a_main.o

a_tex.o: $(SRC)/texture.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/texture.c -o a_tex.o

a_soft.o: $(SRC)/texsoft.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c $(SRC)/texsoft.c -o a_soft.o

# Keep the traditional empty-rule technique so command-line linker options
# take effect without make-specific phony-target syntax. Unix outputs differ.
FORCE:

xgl: testglobe-xgl

testglobe-xgl: $(XGL_OBJECTS) FORCE
	$(CC) -o testglobe-xgl $(XGL_OBJECTS) $(XGL_LDFLAGS)

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

irisgl: testglobe-irisgl

testglobe-irisgl: $(IGL_OBJECTS) FORCE
	$(IGL_CC) -o testglobe-irisgl $(IGL_OBJECTS) $(IGL_LDFLAGS)

i_drv.o: $(SRC)/drv.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/drv.c -o i_drv.o

i_igl.o: $(SRC)/drv_igl.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/drv_igl.c -o i_igl.o

i_err.o: $(SRC)/err.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/err.c -o i_err.o

i_globe.o: $(SRC)/globe.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/globe.c -o i_globe.o

i_main.o: $(SRC)/main.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/main.c -o i_main.o

i_tex.o: $(SRC)/texture.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/texture.c -o i_tex.o

i_soft.o: $(SRC)/texsoft.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c $(SRC)/texsoft.c -o i_soft.o

matrox: testglob.exe

# Each echo is short enough for COMMAND.COM. Response files also keep the
# compiler and linker invocations below DOS's 127-character command limit.
testglob.exe: $(MTX_OBJECTS) $(MTX_LIB) FORCE
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

# Use the 16-bit linker, not the compiler's DOS default startup libraries.
# RC attaches the Earth data resources and marks protected-mode Windows.
# Relink the selected backend because GDI and Matrox share testglob.exe.
gdi: $(GDI_OBJECTS) g_earth.res $(SRC)\gdi.def FORCE
	@echo $(GDI_OBJECTS) $(GDI_LDFLAGS) > g_link.rsp
	@echo testglob.exe >> g_link.rsp
	@echo NUL >> g_link.rsp
	@echo $(GDI_LIBS) >> g_link.rsp
	@echo $(SRC)\gdi.def; >> g_link.rsp
	$(GDI_LINK) @g_link.rsp
	$(GDI_RC) $(GDI_RCFLAGS) g_earth.res testglob.exe

# Resources avoid huge initialized C objects and load in bounded chunks.
g_earth.res: $(SRC)\gdi.rc $(SRC)\earthres.h $(GDI_RESOURCE_DATA)
	$(GDI_RC) -r -fo g_earth.res -i $(SRC) -i $(GDI_DATA) $(SRC)\gdi.rc

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

g_soft.obj: $(SRC)\soft3d.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_soft.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_soft.rsp
	@echo -Fog_soft.obj $(SRC)\soft3d.c >> g_soft.rsp
	$(GDI_CC) @g_soft.rsp

# Keep the Macintosh object/resource format out of the other builds.
quickdraw: FORCE
	$(MPW_EXEC) -f $(QD_MAKEFILE) -d "QD_SRC=$(QD_SOURCE)" -d "QD_DEFINES=$(QD_DEFINES)" quickdraw

clean-quickdraw:
	-$(RM) q_drv.o
	-$(RM) q_qd.o
	-$(RM) q_err.o
	-$(RM) q_globe.o
	-$(RM) q_main.o
	-$(RM) q_fpu.o
	-$(RM) q_soft.o
	-$(RM) TestglobeQD

# Separate targets can clean one backend; "clean" removes all.
# One file per command also works with the original DOS DEL command.
# Ignore missing-file errors from DEL (Unix rm -f is already silent).
clean: clean-opengl clean-solaris-opengl clean-apple-opengl clean-xgl clean-irisgl clean-matrox clean-gdi clean-quickdraw
	-$(RM) testglobe

clean-opengl:
	-$(RM) drv.o
	-$(RM) drv_ogl.o
	-$(RM) err.o
	-$(RM) globe.o
	-$(RM) main.o
	-$(RM) texture.o
	-$(RM) texsoft.o
	-$(RM) testglobe-opengl

clean-solaris-opengl:
	-$(RM) s_drv.o
	-$(RM) s_ogl.o
	-$(RM) s_err.o
	-$(RM) s_globe.o
	-$(RM) s_main.o
	-$(RM) s_tex.o
	-$(RM) s_soft.o
	-$(RM) testglobe-opengl

clean-apple-opengl:
	-$(RM) a_drv.o
	-$(RM) a_ogl.o
	-$(RM) a_err.o
	-$(RM) a_globe.o
	-$(RM) a_main.o
	-$(RM) a_tex.o
	-$(RM) a_soft.o
	-$(RM) testglobe-opengl

clean-xgl:
	-$(RM) x_drv.o
	-$(RM) x_xgl.o
	-$(RM) x_err.o
	-$(RM) x_globe.o
	-$(RM) x_main.o
	-$(RM) x_tex.o
	-$(RM) testglobe-xgl

clean-irisgl:
	-$(RM) i_drv.o
	-$(RM) i_igl.o
	-$(RM) i_err.o
	-$(RM) i_globe.o
	-$(RM) i_main.o
	-$(RM) i_tex.o
	-$(RM) i_soft.o
	-$(RM) testglobe-irisgl

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
	-$(RM) g_soft.obj
	-$(RM) g_drv.rsp
	-$(RM) g_gdi.rsp
	-$(RM) g_err.rsp
	-$(RM) g_globe.rsp
	-$(RM) g_main.rsp
	-$(RM) g_soft.rsp
	-$(RM) g_link.rsp
	-$(RM) g_earth.res
	-$(RM) testglob.exe
