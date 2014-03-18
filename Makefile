sysname := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(sysname),Linux)
	OSYS = linux.o
else
	OSYS = unix.o
endif

CFLAGS =  -Wall -g -Ilibtask

.PHONY: all clean

all: muxer

OFILES=u.o prot.o session.o tags.o stats.o $(OSYS)
HFILES=a.h arg.h muxer.h 
LIB=libtask/libtask.a

libtask/libtask.a:
	$(MAKE) -C libtask libtask.a

muxer: muxer.o $(OFILES) $(HFILES) libtask/libtask.a
	$(CC) -o muxer muxer.o $(OFILES) libtask/libtask.a

clean:
	/bin/rm -f *.o muxer
	$(MAKE) -C libtask clean

