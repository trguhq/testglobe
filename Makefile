CC = cc
DRVFLAGS = -DDRV_OGL -DDRV
CFLAGS = $(DRVFLAGS) -g -O0
LDFLAGS = -lm -lGL -lGLU -lglut

DRV_SRC = drv_ogl.c
#DRV_SRC = drv_pgc.c
#DRV_SRC = drv_xgl.c
#DRV_SRC = drv_igl.c
#DRV_SRC = drv_phg.c

SOURCES = drv.c $(DRV_SRC) err.c globe.c main.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = testglobe

$(TARGET) : $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f $(TARGET) $(OBJECTS) core
