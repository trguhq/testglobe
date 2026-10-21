# Common make syntax for Solaris/System V make and Microsoft C 7 NMAKE.
# Keep this file in step with src/Makefile; only SRC and XGX_ROOT differ.
# Binaries and objects are written in the directory where make is run.

SRC = src

# Portable Unix cc defaults, including older Solaris math/OpenGL libraries.
CC = cc
CPPFLAGS =
CFLAGS = -O
SYS_FLAGS = -DNO_SINF -DNO_GENMIPMAP
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu
OGL_DEFINES = -DFLAT -DDRV_OGL -DTRI -DDRV
OGL_OBJECTS = drv.o drv_ogl.o err.o globe.o main.o texture.o
OGL_HEADERS = $(SRC)/testglob.h $(SRC)/types.h $(SRC)/drv.h \
    $(SRC)/drv_ext.h $(SRC)/drv_ogl.h $(SRC)/err_ext.h \
    $(SRC)/globe.h $(SRC)/glob_ext.h $(SRC)/texture.h $(SRC)/tex_ext.h \
    $(SRC)/e_cmap.h $(SRC)/e_32.h $(SRC)/e_64.h $(SRC)/e_128.h \
    $(SRC)/e_256.h $(SRC)/e_512.h

# Microsoft C 7, huge model, with native DOS paths and 8.3 file names.
MTX_CC = cl
MTX_CFLAGS = -AH -O -W3
MTX_DEFINES = -DSEG86 -DDRV_MTX -DQUAD -DDRV -DNO_SINF
XGX_ROOT = .
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

# Unix default; with DOS NMAKE use: nmake clean RM=del
RM = rm -f

# The first ordinary target is the portable way to choose the default.
opengl: testglobe

testglobe: $(OGL_OBJECTS)
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

# Separate targets can clean one backend; "clean" removes both.
# One file per command also works with the original DOS DEL command.
# Ignore missing-file errors from DEL (Unix rm -f is already silent).
clean: clean-opengl clean-matrox

clean-opengl:
	-$(RM) drv.o
	-$(RM) drv_ogl.o
	-$(RM) err.o
	-$(RM) globe.o
	-$(RM) main.o
	-$(RM) texture.o
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
