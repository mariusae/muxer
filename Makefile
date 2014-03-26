CFLAGS =  -g -Wall -Wextra -Ilibtask -I../libmux

.PHONY: all clean

all: muxer

OFILES=u.o session.o tags.o stats.o
HFILES=a.h arg.h muxer.h 
LIB=libtask/libtask.a

../libmux/libmux:
	$(MAKE) -C ../libmux

libtask/libtask.a:
	$(MAKE) -C libtask libtask.a

muxer: muxer.o $(OFILES) $(HFILES) libtask/libtask.a ../libmux/libmux
	$(CC) -o muxer muxer.o $(OFILES) libtask/libtask.a ../libmux/libmux.a

clean:
	/bin/rm -f *.o muxer
	$(MAKE) -C libtask clean
	$(MAKE) -C ../libmux clean

