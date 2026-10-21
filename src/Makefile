# GNU make, including its MS-DOS port. Run here or use the root Makefile.
# The two backends deliberately use separate object directories and flags.
.DEFAULT_GOAL := opengl

SOURCE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
HEADERS := $(wildcard $(SOURCE_DIR)*.h)

CC ?= cc
CFLAGS ?= -O2
SYS_FLAGS ?= -DGL_GLEXT_PROTOTYPES
LDFLAGS ?= -lm -lglut -lGL -lGLU -lX11
OGL_DEFINES = -DFLAT -DDRV_OGL -DTRI -DDRV
OGL_OBJDIR = objogl
OGL_OBJECTS = $(addprefix $(OGL_OBJDIR)/,drv.o drv_ogl.o err.o globe.o main.o texture.o)

# Microsoft C 7.0, 16-bit huge model. XGX_ROOT contains both XGX1/HEADERS
# and libsv5/lscl.lib. The default places the supplied SDK at the repo root.
MTX_CC ?= cl
MTX_CFLAGS ?= -AH -O -W3
MTX_DEFINES = -DSEG86 -DDRV_MTX -DQUAD -DDRV -DNO_SINF
XGX_ROOT ?= ..
MTX_CPPFLAGS ?= -I$(XGX_ROOT)
MTX_LIB ?= $(XGX_ROOT)/libsv5/lscl.lib
MTX_LDFLAGS ?= -link /STACK:16384
MTX_OBJDIR = objmtx
MTX_OBJECTS = $(addprefix $(MTX_OBJDIR)/,drv.obj drv_mtx.obj err.obj globe.obj main.obj)

# Native DOS needs backslash paths and COMMAND.COM's mkdir/del syntax.
# POSIX is also supported for cross-builds and compiler/SDK validation.
ifneq ($(strip $(COMSPEC)$(ComSpec)),)
mtx_path = $(subst /,\,$(1))
make_dir = if not exist $(1)\NUL mkdir $(1)
else
mtx_path = $(1)
make_dir = mkdir -p $(1)
endif

.PHONY: opengl matrox clean clean-opengl clean-matrox
.DELETE_ON_ERROR:

opengl: testglobe

testglobe: $(OGL_OBJECTS)
	$(CC) -o $@ $(OGL_OBJECTS) $(LDFLAGS)

$(OGL_OBJDIR):
	$(call make_dir,$@)

$(OGL_OBJDIR)/%.o: $(SOURCE_DIR)%.c $(HEADERS) | $(OGL_OBJDIR)
	$(CC) $(CPPFLAGS) $(OGL_DEFINES) $(SYS_FLAGS) $(CFLAGS) -c -o $@ $<

matrox: testglob.exe

# CL response files avoid the 127-character DOS command-line limit.
# All generated DOS basenames fit 8.3, including drv_mtx.obj.
testglob.exe: $(MTX_OBJECTS)
	@echo -AH -Fe$@ > $(call mtx_path,$(MTX_OBJDIR)/link.rsp)
	@echo $(foreach obj,$(MTX_OBJECTS),$(call mtx_path,$(obj))) >> $(call mtx_path,$(MTX_OBJDIR)/link.rsp)
	@echo $(call mtx_path,$(MTX_LIB)) >> $(call mtx_path,$(MTX_OBJDIR)/link.rsp)
	@echo $(MTX_LDFLAGS) >> $(call mtx_path,$(MTX_OBJDIR)/link.rsp)
	$(MTX_CC) @$(call mtx_path,$(MTX_OBJDIR)/link.rsp)

$(MTX_OBJDIR):
	$(call make_dir,$@)

$(MTX_OBJDIR)/%.obj: $(SOURCE_DIR)%.c $(HEADERS) | $(MTX_OBJDIR)
	@echo $(MTX_DEFINES) > $(call mtx_path,$(@:.obj=.rsp))
	@echo $(MTX_CFLAGS) $(call mtx_path,$(MTX_CPPFLAGS)) -c >> $(call mtx_path,$(@:.obj=.rsp))
	@echo -Fo$(call mtx_path,$@) >> $(call mtx_path,$(@:.obj=.rsp))
	@echo $(call mtx_path,$<) >> $(call mtx_path,$(@:.obj=.rsp))
	$(MTX_CC) @$(call mtx_path,$(@:.obj=.rsp))

clean: clean-opengl clean-matrox

ifneq ($(strip $(COMSPEC)$(ComSpec)),)
clean-opengl:
	-del $(OGL_OBJDIR)\*.o
	-del testglobe
clean-matrox:
	-del $(MTX_OBJDIR)\*.obj
	-del $(MTX_OBJDIR)\*.rsp
	-del testglob.exe
else
clean-opengl:
	$(RM) $(OGL_OBJECTS) testglobe
clean-matrox:
	$(RM) $(MTX_OBJECTS) $(MTX_OBJDIR)/*.rsp testglob.exe
endif
