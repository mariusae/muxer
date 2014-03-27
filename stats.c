#include "a.h"
#include "muxer.h"

static void servestatustask(void *v);
static void writestats(int fd);

Stats stats = {
	.nreq = 0,
	.nactivesess = 0,
	.nlifetimesess = 0,
	.nredials = 0,
};

static struct {
	char *name;
	uint64 *vp;
} stattab[] = {
	{ "nreq", &stats.nreq },
	{ "nactivesess", &stats.nactivesess },
	{ "nlifetimesess", &stats.nlifetimesess },
	{ "nredials", &stats.nredials },
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
		
		dtaskstate("serving %s at %d", peer, port);

		writestats(cfd);
		close(cfd);
	}
}

static void
writestats(int fd)
{
	int i;

	fdprint(fd, "HTTP/1.0 200 OK\r\n");
	fdprint(fd, "Content-Type: text\r\n");
	fdprint(fd, "Connection: close\r\n\r\n");
	
	fdprint(fd, "{\n");
	for(i=0; i<nelem(stattab); i++){
		fdprint(fd, "\t\"%s\": %d", stattab[i].name, *stattab[i].vp);
		if(i<nelem(stattab)-1)
			fdprint(fd, ",");
		fdprint(fd, "\n");
	}
		
	fdprint(fd, "}\n");
}
