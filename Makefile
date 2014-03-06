CFLAGS =  -g -Ilibtask

.PHONY: all

all: muxer

OFILES=u.o muxtask.o
LIB=libtask/libtask.a

libtask/libtask.a:
	$(MAKE) -C libtask libtask.a

muxer: muxer.o $(OFILES) libtask/libtask.a
	$(CC) -o muxer muxer.o $(OFILES) libtask/libtask.a

clean:
	/bin/rm -f *.o muxer
	$(MAKE) -C libtask clean
