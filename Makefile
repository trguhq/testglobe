CC = cc
DRVFLAGS = -DDRV_OGL -DDRV
CFLAGS = $(DRVFLAGS) -g -O0
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu

DRV_OBJ = drv_ogl.o
#DRV_SRC = drv_pgc.o
#DRV_SRC = drv_xgl.o
#DRV_SRC = drv_igl.o
#DRV_SRC = drv_phg.o

OBJECTS = drv.o $(DRV_OBJ) err.o globe.o main.o
TARGET = testglobe

$(TARGET) : $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	@rm -f $(TARGET) $(OBJECTS) core
