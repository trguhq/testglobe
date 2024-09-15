# Testglobe Makefile
# Must be configured per platform, default is for OpenGL on Unix.
# Comment what is not needed and uncomment what you need.

CC = cc
#CC = gcc

# DRV_FLAGS selects with driver to use
#
# OpenGL:
DRV_FLAGS = -DDRV_OGL -DDRV
#
# IBM PGC:
#DRV_FLAGS = -DDRV_PGC -DDRV
#
# Sun XGL:
#DRV_FLAGS = -DDRV_XGL -DDRV 
#
# SGI Iris GL:
#DRV_FLAGS = -DDRV_IGL -DDRV
#
# PHIGS
#DRV_FLAGS = -DDRV_PHG -DDRV

# DRV_OBJ is the actual driver file
#
# OpenGL:
DRV_OBJ = drv_ogl.o
#
# IBM PGC:
#DRV_OBJ = drv_pgc.o
#
# Sun XGL:
#DRV_OBJ = drv_xgl.o
#
# SGI Iris GL:
#DRV_OBJ = drv_igl.o
#
# PHIGS
#DRV_OBJ = drv_phg.o

# SYS_FLAGS selects portability options
#
# Solaris 2.8 and earlier:
SYS_FLAGS = -DNO_SINF -DNO_GENMIPMAP
#
# IRIX, OS X/macOS:
#SYS_FLAGS =

# Release:
CFLAGS = $(DRV_FLAGS) $(SYS_FLAGS) -O2
# Debug:
#CFLAGS = $(DRV_FLAGS) $(SYS_FLAGS) -g -O0 -DDEBUG

# LDFLAGS varies per combination of driver and system
#
# OpenGL in Unix-compatible environment:
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu

OBJECTS = drv.o $(DRV_OBJ) err.o globe.o main.o texture.o
TARGET = testglobe

$(TARGET) : $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	@rm -f $(TARGET) $(OBJECTS) core
