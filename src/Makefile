CC = g++
objects = areadb.o dbcfile.o font.o frustum.o liquid.o particle.o maptile.o menu.o model.o mpq_stormlib.o shaders.o sky.o test.o video.o wmo.o world.o wowmapview.o util.o

all:	wowmapview

clean:
	rm -f wowmapview *.o

wowmapview: $(objects) stormlib/libStorm.a
	$(CC) -o $@ $+ -L/usr/X11R6/lib -lSDL -lGL -lGLU -lbz2

clean_mpq:
	libmpq/make clean
clean_zlib:
	zlib/make clean

stormlib/libStorm.a:
	cd stormlib;make -f Makefile.linux
zlib/zlib.a:
	zlib/make

%.o:%.cpp
	$(CC) -c $+ -ISDL/include/ -D_LINUX
