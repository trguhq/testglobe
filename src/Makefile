# Common make syntax for IRIX/Solaris System V make and Microsoft C 7 NMAKE.
# Keep this file in step with ../Makefile; directory defaults differ.
# Binaries and objects are written in the directory where make is run.
# Driver headers select rendering callbacks; drv.c contains shared helpers.

SRC = .

# Portable Unix cc defaults, including older Solaris math/OpenGL libraries.
CC = cc
CPPFLAGS =
CFLAGS = -O
SYS_FLAGS = -DNO_SINF
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu
# drv_ogl.h selects latitude strips with separately batched pole triangles.
OGL_DEFINES = -DFLAT -DDRV_OGL -DDRV
OGL_OBJECTS = drv.o drv_ogl.o err.o globe.o main.o texture.o texsoft.o e_data.o
OGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_ogl.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/e_ext.h \
    $(SRC)/texture.h $(SRC)/tex_ext.h $(SRC)/texsoft.h \
    $(SRC)/e_cmap.h $(SRC)/earthres.h

# Native Solaris OpenGL/GLUT with the installed OpenWindows headers/libraries.
# s_ objects keep Solaris compiler and linker options separate from other builds.
OPENWINHOME = /usr/openwin
SOLARIS_CC = $(CC)
SOLARIS_CPPFLAGS = -I$(OPENWINHOME)/include
SOLARIS_CFLAGS = $(CFLAGS)
SOLARIS_SYS_FLAGS = -DNO_SINF
SOLARIS_DEFINES = $(OGL_DEFINES)
SOLARIS_LDFLAGS = -L$(OPENWINHOME)/lib -R$(OPENWINHOME)/lib \
    -lglut -lGL -lGLU -lX11 -lXmu -lm
SOLARIS_OBJECTS = s_drv.o s_ogl.o s_err.o s_globe.o s_main.o s_tex.o s_soft.o s_data.o

# Native macOS OpenGL/GLUT from the selected Xcode command line tools.
# Apple objects stay separate from Unix GL/X11 objects. Apple's legacy
# OpenGL texturing uses the same bilinear, OpenGL 1.1 path on every target.
APPLE_CC = $(CC)
APPLE_CPPFLAGS =
APPLE_CFLAGS = $(CFLAGS)
APPLE_SYS_FLAGS = -DGLUT_SILENCE_DEPRECATION
APPLE_DEFINES = $(OGL_DEFINES)
APPLE_LDFLAGS = -framework GLUT -framework OpenGL -framework Cocoa
APPLE_OBJECTS = a_drv.o a_ogl.o a_err.o a_globe.o a_main.o a_tex.o a_soft.o a_data.o

# PiStorm3D through the shared MiniGL V29 / dispatch ABI 5 interface.
# MINIGL_ROOT is the SDK checkout containing libraryroot/include; the root
# include directory in older releases has an incompatible dispatch ABI.
# The application calls the shared library directly, without a GCC import archive.
AOGL_CC = vc
AOGL_CONFIG = +aos68k
AOGL_CFLAGS = -c99 -O1 -cpu=68020 -fpu=68881
AOGL_CPPFLAGS =
MINIGL_ROOT = .
AOGL_INCLUDES = -I$(MINIGL_ROOT)/libraryroot/include
AOGL_DEFINES = $(OGL_DEFINES) -DOGL_AMIGA -DNO_SINF
AOGL_LDFLAGS =
AOGL_LIBS = -lm881 -lamiga
AOGL_PYTHON = python3
AOGL_OBJECTS = ao_drv.o ao_ogl.o ao_ami.o ao_err.o ao_globe.o ao_main.o ao_tex.o ao_soft.o ao_data.o
AOGL_HEADERS = $(OGL_HEADERS) $(SRC)/ogl_ami.h
AOGL_SDK_HEADERS = $(MINIGL_ROOT)/libraryroot/include/proto/minigl.h \
    $(MINIGL_ROOT)/libraryroot/include/libraries/minigl.h \
    $(MINIGL_ROOT)/libraryroot/include/libraries/minigl_dispatch.h \
    $(MINIGL_ROOT)/libraryroot/include/mgl/gl.h \
    $(MINIGL_ROOT)/libraryroot/include/mgl/context.h

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
XGL_OBJECTS = x_drv.o x_xgl.o x_err.o x_globe.o x_main.o x_tex.o x_data.o
XGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_xgl.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/e_ext.h $(SRC)/texture.h $(SRC)/tex_ext.h \
    $(SRC)/e_cmap.h $(SRC)/earthres.h

# Native IRIS GL on 32-bit MIPS IRIX 4.0. These shared-library names follow
# the IRIS GL Programming Guide; override for the installed SGI toolchain.
# i_ objects keep this driver separate from both other Unix backends.
IGL_CC = $(CC)
IGL_CPPFLAGS =
IGL_CFLAGS = $(CFLAGS)
IGL_SYS_FLAGS = -DNO_SINF
IGL_DEFINES = -DFLAT -DDRV_IGL -DDRV
IGL_LDFLAGS = -lc_s -lgl_s -lm
IGL_OBJECTS = i_drv.o i_igl.o i_err.o i_globe.o i_main.o i_tex.o i_soft.o i_data.o
IGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_igl.h $(SRC)/texsoft.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/e_ext.h $(SRC)/texture.h $(SRC)/tex_ext.h \
    $(SRC)/e_cmap.h $(SRC)/earthres.h

# Microsoft C 7, huge model, with native DOS paths and 8.3 file names.
MTX_CC = cl
MTX_CFLAGS = -AH -O -W3
MTX_DEFINES = -DSEG86 -DDRV_MTX -DDRV -DNO_SINF
XGX_ROOT = ..
MTX_CPPFLAGS = -I$(XGX_ROOT)
MTX_LIB = $(XGX_ROOT)\libsv5\lscl.lib
MTX_LDFLAGS = -link /STACK:16384
MTX_OBJECTS = drv.obj drv_mtx.obj err.obj globe.obj main.obj e_data.obj
MTX_HEADERS = $(SRC)\testglob.h $(SRC)\types.h $(SRC)\drv.h \
    $(SRC)\drv_ext.h $(SRC)\drv_mtx.h $(SRC)\err_ext.h \
    $(SRC)\globe.h $(SRC)\glob_ext.h $(SRC)\e_ext.h $(SRC)\e_cmap.h \
    $(SRC)\earthres.h \
    $(XGX_ROOT)\XGX1\HEADERS\DEF.H \
    $(XGX_ROOT)\XGX1\HEADERS\LIBFN.H \
    $(XGX_ROOT)\XGX1\HEADERS\IOLIB.H

# TIGA 2.0, Microsoft C 7 huge model. The vendor communication driver and
# graphics manager supply board-specific support; ai.lib uses far code/data.
# Each t_ object is independent of the other DOS and Windows drivers.
# Baseline code runs before CPU detection. Only t_386.obj uses 386 opcodes;
# /FPi retains the DOS emulator runtime for machines without an x87.
TGA_CC = cl
TGA_CFLAGS = -AH -G0 -FPi -O -W3
TGA_386FLAGS = -G3 -DSOFT3D_TRANSFORM_386
TGA_DEFINES = -DSEG86 -DDRV_TGA -DDRV -DNO_SINF
TGA_ROOT = ..\TIGA
TGA_CPPFLAGS = -I$(TGA_ROOT)\include
TGA_LIB = $(TGA_ROOT)\libs\ai.lib
TGA_LDFLAGS = -link /STACK:16384
# TI tools build the downloadable TMS34010 module, also usable on TMS34020.
# The module is a build input embedded in testglob.exe, not a runtime sidecar.
# TGA_MODULE= builds a standalone executable with software transforms only.
TGA_MODULE = tga_gsp.rlm
TGA_GSP_CC = gspcl
TGA_GSP_LINK = gsplnk
TGA_GSP_CFLAGS = -c -o -v10
TGA_GSP_CPPFLAGS = -i$(SRC)
TGA_GSP_LDFLAGS = -r -cr
TGA_OBJECTS = t_drv.obj t_tga.obj t_cpu.obj t_err.obj t_globe.obj t_main.obj t_soft.obj t_386.obj t_float.obj t_data.obj
TGA_HEADERS = $(SRC)\testglob.h $(SRC)\types.h $(SRC)\drv.h \
    $(SRC)\drv_ext.h $(SRC)\drv_tga.h $(SRC)\err_ext.h \
    $(SRC)\globe.h $(SRC)\glob_ext.h $(SRC)\e_ext.h $(SRC)\e_cmap.h \
    $(SRC)\earthres.h $(SRC)\soft3d.h $(SRC)\tga_cpu.h $(SRC)\tga_cmd.h
TGA_SDK_HEADERS = $(TGA_ROOT)\include\tiga.h \
    $(TGA_ROOT)\include\extend.h $(TGA_ROOT)\include\typedefs.h

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
GDI_OBJECTS = g_drv.obj g_gdi.obj g_err.obj g_globe.obj g_main.obj g_soft.obj g_float.obj
GDI_HEADERS = $(SRC)\testglob.h $(SRC)\types.h $(SRC)\drv.h \
    $(SRC)\drv_ext.h $(SRC)\drv_gdi.h $(SRC)\err_ext.h \
    $(SRC)\globe.h $(SRC)\glob_ext.h $(SRC)\e_ext.h $(SRC)\e_cmap.h \
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

# VBCC cross-build for AmigaOS 1.3 and later. The kick13 runtime and msoft
# keep startup and baseline floating point usable on an original 68000.
# Newer NDK headers are needed for version-guarded display/palette queries.
# Fullscreen screen-buffer swapping is enabled at runtime on AmigaOS 3.0+.
# Override AMI_CPPFLAGS when those headers are outside the configured path.
AMI_CC = vc
AMI_CONFIG = +kick13
AMI_CFLAGS = -O1 -cpu=68000 -fpu=0 -rmcfg-amiga-softfloat
AMI_020FLAGS = -O1 -cpu=68020 -fpu=0 -rmcfg-amiga-softfloat
# Basic FPU arithmetic also runs on 68040/68060; the Amiga projection
# helper avoids FINTRZ and transcendental opcodes. Keep the pointer-only ABI.
AMI_FPUFLAGS = -O1 -cpu=68020 -fpu=68881 -rmcfg-amiga-softfloat
AMI_CPPFLAGS =
# Optional runtime CyberGraphX/Picasso96 support; 0 builds native chips only.
AMI_RTG = 1
AMI_DEFINES = -DFLAT -DDRV_AMI -DDRV -DNO_SINF -DAMI_RTG=$(AMI_RTG)
AMI_LDFLAGS =
AMI_LIBS = -lmsoft -lamiga
AMI_PYTHON = python3
AMI_OBJECTS = am_drv.o am_ami.o am_data.o am_err.o am_globe.o am_main.o am_soft.o am_020.o am_float.o
AMI_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_ami.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/e_ext.h $(SRC)/e_cmap.h \
    $(SRC)/earthres.h $(SRC)/soft3d.h

# Binary maps are build inputs only. Keep the helper independent of any SDK.
# A checked-in src/e_saved.c is available for compilers used outside make.
EARTH_CC = $(CC)
EARTH_CFLAGS = $(CFLAGS)
EARTH_DATA = $(SRC)/../data
EARTH_INPUTS = $(EARTH_DATA)/e32.bin $(EARTH_DATA)/e64.bin \
    $(EARTH_DATA)/e128.bin $(EARTH_DATA)/e256_0.bin $(EARTH_DATA)/e256_1.bin \
    $(EARTH_DATA)/e512_0.bin $(EARTH_DATA)/e512_1.bin $(EARTH_DATA)/e512_2.bin \
    $(EARTH_DATA)/e512_3.bin $(EARTH_DATA)/e512_4.bin $(EARTH_DATA)/e512_5.bin \
    $(EARTH_DATA)/e512_6.bin $(EARTH_DATA)/e512_7.bin
MTX_DATA = $(SRC)\..\data
MTX_INPUTS = $(MTX_DATA)\e32.bin $(MTX_DATA)\e64.bin \
    $(MTX_DATA)\e128.bin $(MTX_DATA)\e256_0.bin $(MTX_DATA)\e256_1.bin \
    $(MTX_DATA)\e512_0.bin $(MTX_DATA)\e512_1.bin $(MTX_DATA)\e512_2.bin \
    $(MTX_DATA)\e512_3.bin $(MTX_DATA)\e512_4.bin $(MTX_DATA)\e512_5.bin \
    $(MTX_DATA)\e512_6.bin $(MTX_DATA)\e512_7.bin

# Unix default; with DOS NMAKE use: nmake clean RM=del
RM = rm -f

# The first ordinary target is the portable way to choose the default.
opengl: testglobe-opengl

# Regenerate automatically when a binary map or converter changes. The DOS
# source uses a different name so native compiler commands need no make dialect.
e_data.c: $(SRC)/e_bin.c $(SRC)/buildio.h $(SRC)/earthres.h $(EARTH_INPUTS)
	$(EARTH_CC) $(EARTH_CFLAGS) -o e_bin $(SRC)/e_bin.c
	./e_bin $(EARTH_DATA) e_data.c

m_data.c: $(SRC)\e_bin.c $(SRC)\buildio.h $(SRC)\earthres.h $(MTX_INPUTS)
	@echo -O -W3 -Fee_bin.exe > e_bin.rsp
	@echo $(SRC)\e_bin.c >> e_bin.rsp
	$(MTX_CC) @e_bin.rsp
	e_bin $(MTX_DATA) m_data.c

t_data.c: $(SRC)\e_bin.c $(SRC)\buildio.h $(SRC)\earthres.h $(MTX_INPUTS)
	@echo -O -W3 -Fee_bin.exe > t_bin.rsp
	@echo $(SRC)\e_bin.c >> t_bin.rsp
	$(TGA_CC) @t_bin.rsp
	e_bin $(MTX_DATA) t_data.c

# Explicitly refresh the checked-in generated source after editing the maps.
earth-data: $(SRC)/e_bin.c $(SRC)/buildio.h $(SRC)/earthres.h $(EARTH_INPUTS) FORCE
	$(EARTH_CC) $(EARTH_CFLAGS) -o e_bin $(SRC)/e_bin.c
	./e_bin $(EARTH_DATA) $(SRC)/e_saved.c

earth-data-dos: $(SRC)\e_bin.c $(SRC)\buildio.h $(SRC)\earthres.h $(MTX_INPUTS) FORCE
	@echo -O -W3 -Fee_bin.exe > e_bin.rsp
	@echo $(SRC)\e_bin.c >> e_bin.rsp
	$(MTX_CC) @e_bin.rsp
	e_bin $(MTX_DATA) $(SRC)\e_saved.c

e_data.o: e_data.c $(OGL_HEADERS)
	$(CC) $(CPPFLAGS) -I$(SRC) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c e_data.c

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

s_data.o: e_data.c $(OGL_HEADERS)
	$(SOLARIS_CC) $(CPPFLAGS) $(SOLARIS_CPPFLAGS) -I$(SRC) $(SOLARIS_DEFINES) $(SOLARIS_SYS_FLAGS) $(SOLARIS_CFLAGS) -c e_data.c -o s_data.o

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

a_data.o: e_data.c $(OGL_HEADERS)
	$(APPLE_CC) $(CPPFLAGS) $(APPLE_CPPFLAGS) -I$(SRC) $(APPLE_DEFINES) $(APPLE_SYS_FLAGS) $(APPLE_CFLAGS) -c e_data.c -o a_data.o

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

# Amiga uses MiniGL for rendering and Intuition for its window and input.
amiga-opengl: testglobe-amiga-opengl testglobe-amiga-opengl.info

testglobe-amiga-opengl: $(AOGL_OBJECTS) FORCE
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_LDFLAGS) $(AOGL_OBJECTS) $(AOGL_LIBS) -o testglobe-amiga-opengl

testglobe-amiga-opengl.info: $(SRC)/ami_pack.py
	$(AOGL_PYTHON) $(SRC)/ami_pack.py --icon testglobe-amiga-opengl.info --stack 131072

ao_data.o: e_data.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c e_data.c -o ao_data.o

ao_drv.o: $(SRC)/drv.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/drv.c -o ao_drv.o

ao_ogl.o: $(SRC)/drv_ogl.c $(AOGL_HEADERS) $(AOGL_SDK_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/drv_ogl.c -o ao_ogl.o

ao_ami.o: $(SRC)/ogl_ami.c $(AOGL_HEADERS) $(AOGL_SDK_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/ogl_ami.c -o ao_ami.o

ao_err.o: $(SRC)/err.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/err.c -o ao_err.o

ao_globe.o: $(SRC)/globe.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/globe.c -o ao_globe.o

ao_main.o: $(SRC)/main.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/main.c -o ao_main.o

ao_tex.o: $(SRC)/texture.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/texture.c -o ao_tex.o

ao_soft.o: $(SRC)/texsoft.c $(AOGL_HEADERS)
	$(AOGL_CC) $(AOGL_CONFIG) $(AOGL_CFLAGS) $(AOGL_CPPFLAGS) $(AOGL_INCLUDES) $(AOGL_DEFINES) -I$(SRC) -c $(SRC)/texsoft.c -o ao_soft.o

# Keep the traditional empty-rule technique so command-line linker options
# take effect without make-specific phony-target syntax. Unix outputs differ.
FORCE:

xgl: testglobe-xgl

testglobe-xgl: $(XGL_OBJECTS) FORCE
	$(CC) -o testglobe-xgl $(XGL_OBJECTS) $(XGL_LDFLAGS)

x_data.o: e_data.c $(XGL_HEADERS)
	$(CC) $(CPPFLAGS) $(XGL_CPPFLAGS) -I$(SRC) $(XGL_DEFINES) $(XGL_SYS_FLAGS) $(XGL_CFLAGS) -c e_data.c -o x_data.o

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

i_data.o: e_data.c $(IGL_HEADERS)
	$(IGL_CC) $(CPPFLAGS) $(IGL_CPPFLAGS) -I$(SRC) $(IGL_DEFINES) $(IGL_SYS_FLAGS) $(IGL_CFLAGS) -c e_data.c -o i_data.o

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

e_data.obj: m_data.c $(MTX_HEADERS)
	@echo $(MTX_DEFINES) $(MTX_CFLAGS) > e_data.rsp
	@echo $(MTX_CPPFLAGS) -I$(SRC) -c >> e_data.rsp
	@echo -Foe_data.obj m_data.c >> e_data.rsp
	$(MTX_CC) @e_data.rsp

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

# Relink because the selected DOS/Win16 target shares the 8.3 output name.
tiga: $(TGA_OBJECTS) $(TGA_LIB) $(TGA_MODULE) tga_pack.exe FORCE
	@echo -AH -FPi -Fetga_host.exe > t_link.rsp
	@echo $(TGA_OBJECTS) >> t_link.rsp
	@echo $(TGA_LIB) $(TGA_LDFLAGS) >> t_link.rsp
	$(TGA_CC) @t_link.rsp
	tga_pack.exe tga_host.exe testglob.exe $(TGA_MODULE)

# The packer preserves the MZ load image and publishes the final EXE safely.
tga_pack.exe: $(SRC)\tga_pack.c $(SRC)\buildio.h $(SRC)\tga_cmd.h
	@echo $(TGA_CFLAGS) -Fetga_pack.exe > t_pack.rsp
	@echo $(SRC)\tga_pack.c >> t_pack.rsp
	$(TGA_CC) @t_pack.rsp

# This module contains portable fixed-point code, not host x86 objects.
tiga-module: tga_gsp.rlm

tga_gsp.rlm: tga_gsp.obj tga_ext.obj
	$(TGA_GSP_LINK) -o tga_gsp.rlm $(TGA_GSP_LDFLAGS) tga_gsp.obj tga_ext.obj

tga_gsp.obj: $(SRC)\tga_gsp.c $(SRC)\tga_cmd.h
	$(TGA_GSP_CC) $(TGA_GSP_CFLAGS) $(TGA_GSP_CPPFLAGS) $(SRC)\tga_gsp.c

tga_ext.obj: $(SRC)\tga_ext.asm
	$(TGA_GSP_CC) -c $(SRC)\tga_ext.asm

t_drv.obj: $(SRC)\drv.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_drv.rsp
	@echo $(TGA_CFLAGS) >> t_drv.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_drv.rsp
	@echo -Fot_drv.obj $(SRC)\drv.c >> t_drv.rsp
	$(TGA_CC) @t_drv.rsp

t_tga.obj: $(SRC)\drv_tga.c $(TGA_HEADERS) $(TGA_SDK_HEADERS)
	@echo $(TGA_DEFINES) > t_tga.rsp
	@echo $(TGA_CFLAGS) >> t_tga.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_tga.rsp
	@echo -Fot_tga.obj $(SRC)\drv_tga.c >> t_tga.rsp
	$(TGA_CC) @t_tga.rsp

t_cpu.obj: $(SRC)\tga_cpu.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_cpu.rsp
	@echo $(TGA_CFLAGS) >> t_cpu.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_cpu.rsp
	@echo -Fot_cpu.obj $(SRC)\tga_cpu.c >> t_cpu.rsp
	$(TGA_CC) @t_cpu.rsp

t_err.obj: $(SRC)\err.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_err.rsp
	@echo $(TGA_CFLAGS) >> t_err.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_err.rsp
	@echo -Fot_err.obj $(SRC)\err.c >> t_err.rsp
	$(TGA_CC) @t_err.rsp

t_globe.obj: $(SRC)\globe.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_globe.rsp
	@echo $(TGA_CFLAGS) >> t_globe.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_globe.rsp
	@echo -Fot_globe.obj $(SRC)\globe.c >> t_globe.rsp
	$(TGA_CC) @t_globe.rsp

t_main.obj: $(SRC)\main.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_main.rsp
	@echo $(TGA_CFLAGS) >> t_main.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_main.rsp
	@echo -Fot_main.obj $(SRC)\main.c >> t_main.rsp
	$(TGA_CC) @t_main.rsp

t_soft.obj: $(SRC)\soft3d.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_soft.rsp
	@echo $(TGA_CFLAGS) >> t_soft.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_soft.rsp
	@echo -Fot_soft.obj $(SRC)\soft3d.c >> t_soft.rsp
	$(TGA_CC) @t_soft.rsp

t_386.obj: $(SRC)\soft3d.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_386.rsp
	@echo $(TGA_CFLAGS) $(TGA_386FLAGS) >> t_386.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_386.rsp
	@echo -Fot_386.obj $(SRC)\soft3d.c >> t_386.rsp
	$(TGA_CC) @t_386.rsp

t_float.obj: $(SRC)\soft3fp.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_float.rsp
	@echo $(TGA_CFLAGS) >> t_float.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_float.rsp
	@echo -Fot_float.obj $(SRC)\soft3fp.c >> t_float.rsp
	$(TGA_CC) @t_float.rsp

t_data.obj: t_data.c $(TGA_HEADERS)
	@echo $(TGA_DEFINES) > t_data.rsp
	@echo $(TGA_CFLAGS) >> t_data.rsp
	@echo $(TGA_CPPFLAGS) -I$(SRC) -c >> t_data.rsp
	@echo -Fot_data.obj t_data.c >> t_data.rsp
	$(TGA_CC) @t_data.rsp

# Use the 16-bit linker, not the compiler's DOS default startup libraries.
# RC attaches the Earth data resources and marks protected-mode Windows.
# Relink the selected backend because DOS and Win16 targets share testglob.exe.
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

g_float.obj: $(SRC)\soft3fp.c $(GDI_HEADERS)
	@echo $(GDI_DEFINES) > g_float.rsp
	@echo $(GDI_CFLAGS) $(GDI_CPPFLAGS) -c >> g_float.rsp
	@echo -Fog_float.obj $(SRC)\soft3fp.c >> g_float.rsp
	$(GDI_CC) @g_float.rsp

# Resources are skipped by LoadSeg and read from this executable on demand.
amiga: testglobe-amiga testglobe-amiga.info

testglobe-amiga: $(AMI_OBJECTS) $(SRC)/ami_pack.py $(EARTH_INPUTS) FORCE
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_LDFLAGS) $(AMI_OBJECTS) $(AMI_LIBS) -o am_link.hunk
	$(AMI_PYTHON) $(SRC)/ami_pack.py am_link.hunk $(EARTH_DATA) testglobe-amiga

testglobe-amiga.info: $(SRC)/ami_pack.py
	$(AMI_PYTHON) $(SRC)/ami_pack.py --icon testglobe-amiga.info

am_drv.o: $(SRC)/drv.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/drv.c -o am_drv.o

# Recompile when invoked so changing AMI_RTG cannot reuse the other variant.
am_ami.o: $(SRC)/drv_ami.c $(AMI_HEADERS) FORCE
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/drv_ami.c -o am_ami.o

am_data.o: $(SRC)/ami_data.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/ami_data.c -o am_data.o

am_err.o: $(SRC)/err.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/err.c -o am_err.o

am_globe.o: $(SRC)/globe.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/globe.c -o am_globe.o

am_main.o: $(SRC)/main.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/main.c -o am_main.o

am_soft.o: $(SRC)/soft3d.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_CFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/soft3d.c -o am_soft.o

# These objects are entered only after Exec CPU/FPU detection.
am_020.o: $(SRC)/soft3d.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_020FLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -DSOFT3D_TRANSFORM_020 -I$(SRC) -c $(SRC)/soft3d.c -o am_020.o

am_float.o: $(SRC)/soft3fp.c $(AMI_HEADERS)
	$(AMI_CC) $(AMI_CONFIG) $(AMI_FPUFLAGS) $(AMI_CPPFLAGS) $(AMI_DEFINES) -I$(SRC) -c $(SRC)/soft3fp.c -o am_float.o

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
	-$(RM) q_float.o
	-$(RM) TestglobeQD

# Separate targets can clean one backend; "clean" removes all.
# One file per command also works with the original DOS DEL command.
# Ignore missing-file errors from DEL (Unix rm -f is already silent).
clean: clean-opengl clean-solaris-opengl clean-apple-opengl clean-amiga-opengl clean-xgl clean-irisgl clean-matrox clean-tiga clean-gdi clean-quickdraw clean-amiga clean-earth-data
	-$(RM) testglobe

clean-opengl:
	-$(RM) e_data.o
	-$(RM) drv.o
	-$(RM) drv_ogl.o
	-$(RM) err.o
	-$(RM) globe.o
	-$(RM) main.o
	-$(RM) texture.o
	-$(RM) texsoft.o
	-$(RM) testglobe-opengl

clean-solaris-opengl:
	-$(RM) s_data.o
	-$(RM) s_drv.o
	-$(RM) s_ogl.o
	-$(RM) s_err.o
	-$(RM) s_globe.o
	-$(RM) s_main.o
	-$(RM) s_tex.o
	-$(RM) s_soft.o
	-$(RM) testglobe-opengl

clean-apple-opengl:
	-$(RM) a_data.o
	-$(RM) a_drv.o
	-$(RM) a_ogl.o
	-$(RM) a_err.o
	-$(RM) a_globe.o
	-$(RM) a_main.o
	-$(RM) a_tex.o
	-$(RM) a_soft.o
	-$(RM) testglobe-opengl

clean-amiga-opengl:
	-$(RM) ao_data.o
	-$(RM) ao_drv.o
	-$(RM) ao_ogl.o
	-$(RM) ao_ami.o
	-$(RM) ao_err.o
	-$(RM) ao_globe.o
	-$(RM) ao_main.o
	-$(RM) ao_tex.o
	-$(RM) ao_soft.o
	-$(RM) testglobe-amiga-opengl
	-$(RM) testglobe-amiga-opengl.info

clean-xgl:
	-$(RM) x_data.o
	-$(RM) x_drv.o
	-$(RM) x_xgl.o
	-$(RM) x_err.o
	-$(RM) x_globe.o
	-$(RM) x_main.o
	-$(RM) x_tex.o
	-$(RM) testglobe-xgl

clean-irisgl:
	-$(RM) i_data.o
	-$(RM) i_drv.o
	-$(RM) i_igl.o
	-$(RM) i_err.o
	-$(RM) i_globe.o
	-$(RM) i_main.o
	-$(RM) i_tex.o
	-$(RM) i_soft.o
	-$(RM) testglobe-irisgl

clean-matrox:
	-$(RM) e_data.obj
	-$(RM) e_data.rsp
	-$(RM) m_data.c
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

clean-tiga:
	-$(RM) tga_host.exe
	-$(RM) tga_pack.exe
	-$(RM) tga_pack.obj
	-$(RM) t_pack.rsp
	-$(RM) testglob.tmp
	-$(RM) tga_gsp.rlm
	-$(RM) tga_gsp.obj
	-$(RM) tga_gsp.asm
	-$(RM) tga_ext.obj
	-$(RM) t_drv.obj
	-$(RM) t_tga.obj
	-$(RM) t_cpu.obj
	-$(RM) t_err.obj
	-$(RM) t_globe.obj
	-$(RM) t_main.obj
	-$(RM) t_soft.obj
	-$(RM) t_386.obj
	-$(RM) t_float.obj
	-$(RM) t_data.obj
	-$(RM) t_drv.rsp
	-$(RM) t_tga.rsp
	-$(RM) t_cpu.rsp
	-$(RM) t_err.rsp
	-$(RM) t_globe.rsp
	-$(RM) t_main.rsp
	-$(RM) t_soft.rsp
	-$(RM) t_386.rsp
	-$(RM) t_float.rsp
	-$(RM) t_data.rsp
	-$(RM) t_bin.rsp
	-$(RM) t_link.rsp
	-$(RM) t_data.c
	-$(RM) t_data.tmp
	-$(RM) testglob.exe

clean-gdi:
	-$(RM) g_drv.obj
	-$(RM) g_gdi.obj
	-$(RM) g_err.obj
	-$(RM) g_globe.obj
	-$(RM) g_main.obj
	-$(RM) g_soft.obj
	-$(RM) g_float.obj
	-$(RM) g_drv.rsp
	-$(RM) g_gdi.rsp
	-$(RM) g_err.rsp
	-$(RM) g_globe.rsp
	-$(RM) g_main.rsp
	-$(RM) g_soft.rsp
	-$(RM) g_float.rsp
	-$(RM) g_link.rsp
	-$(RM) g_earth.res
	-$(RM) testglob.exe

clean-amiga:
	-$(RM) am_drv.o
	-$(RM) am_ami.o
	-$(RM) am_data.o
	-$(RM) am_err.o
	-$(RM) am_globe.o
	-$(RM) am_main.o
	-$(RM) am_soft.o
	-$(RM) am_020.o
	-$(RM) am_float.o
	-$(RM) am_link.hunk
	-$(RM) testglobe-amiga
	-$(RM) testglobe-amiga.info

# Build copies are disposable; preserve the checked-in e_saved.c and backups.
clean-earth-data:
	-$(RM) e_data.c
	-$(RM) m_data.c
	-$(RM) t_data.c
	-$(RM) e_bin
	-$(RM) e_bin.exe
	-$(RM) e_bin.obj
	-$(RM) e_bin.rsp
	-$(RM) e_data.tmp
	-$(RM) m_data.tmp
	-$(RM) t_data.tmp
	-$(RM) e_saved.tmp
