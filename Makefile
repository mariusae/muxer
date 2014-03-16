CFLAGS =  -g -Wall -Wextra -Ilibtask -I../libmux

.PHONY: all

all: muxer

OFILES=u.o prot.o session.o tags.o
LIB=libtask/libtask.a

../libmux/libmux.a:
	$(MAKE) -C ../libmux

libtask/libtask.a:
	$(MAKE) -C libtask libtask.a

muxer: muxer.o $(OFILES) libtask/libtask.a ../libmux/libmux.a
	$(CC) -o muxer muxer.o $(OFILES) libtask/libtask.a ../libmux/libmux.a

clean:
	/bin/rm -f *.o muxer
	$(MAKE) -C libtask clean
	$(MAKE) -C ../libmux clean
