#include "a.h"
#include "muxer.h"

static void servestatustask(void *v);
static void writestats(int fd);

Stats stats = {
	.nreq = 0
};

void
servestatus(int fd)
{
	taskcreate(servestatustask, (void*)(uintptr)fd, STACK);
}

static void
servestatustask(void *v)
{
	char peer[16];
	int port, fd, cfd;
	
	fd = (uintptr)v;
	
	taskname("servestatus");

	for(;;){
		if((cfd = netaccept(fd, peer, &port)) < 0){
			fprint(2, "netaccept: %r");
			break;
		}
		
		taskstate("serving %s at %d", peer, port);

		writestats(cfd);
		close(cfd);
	}
}

static void
writestats(int fd)
{
	fdprint(fd, "HTTP/1.0 200 OK\r\n");
	fdprint(fd, "Content-Type: text\r\n");
	fdprint(fd, "Connection: close\r\n\r\n");
	fdprint(fd, "nreq %d\n", stats.nreq);
}
