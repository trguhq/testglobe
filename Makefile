CC = cc
DRVFLAGS = -DDRV_OGL -DDRV
CFLAGS = $(DRVFLAGS) -g -O0
LDFLAGS = -lm -lglut -lGL -lGLU -lX11 -lXmu

DRV_OBJ = drv_ogl.o
#DRV_OBJ = drv_pgc.o
#DRV_OBJ = drv_xgl.o
#DRV_OBJ = drv_igl.o
#DRV_OBJ = drv_phg.o

OBJECTS = drv.o $(DRV_OBJ) err.o globe.o main.o texture.o
TARGET = testglobe

$(TARGET) : $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	@rm -f $(TARGET) $(OBJECTS) core
