CC = g++
objects = areadb.o dbcfile.o font.o frustum.o liquid.o particle.o maptile.o menu.o model.o mpq_stormlib.o shaders.o sky.o test.o video.o wmo.o world.o wowmapview.o util.o

all:	wowmapview

clean:
	rm -f wowmapview *.o

wowmapview: $(objects) stormlib/libStorm.a
	$(CC) -o $@ $+ -L/usr/X11R6/lib -lSDL -lGL -lGLU -lbz2
	cp "$@" ../bin/

clean_mpq:
	libmpq/make clean
clean_zlib:
	zlib/make clean

stormlib/libStorm.a:
	cd stormlib;make -f Makefile.linux
zlib/zlib.a:
	zlib/make

mpq_stormlib.o: mpq_stormlib.h util.h wowmapview.h appstate.h font.h
wowmapview.o: util.h mpq.h mpq_stormlib.h video.h vec3d.h manager.h font.h ddslib.h appstate.h test.h world.h wowmapview.h maptile.h wmo.h model.h modelheaders.h quaternion.h matrix.h animated.h defines.h particle.h liquid.h frustum.h sky.h dbcfile.h menu.h areadb.h
%.o:%.cpp
	$(CC) -c $< -D_LINUX -g
dbclister: dbclister.o dbcfile.o mpq_stormlib.o util.o stormlib/libStorm.a
	$(CC) -o $@ $+ -lbz2
