CC = cc
CFLAGS = -g -O0
LDFLAGS = -lm -lGL -lGLUT
DRVFLAGS = -DDRV_OGL

DRV_SRC = drv_ogl.c
#DRV_SRC = drv_pgc.c
#DRV_SRC = drv_xgl.c

SOURCES = drv.c $(DRV_SRC) err.c globe.c main.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = testglobe

$(TARGET) : $(OBJECTS)
    $(CC) $(DRVFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
    @rm -f $(TARGET) $(OBJECTS) core
