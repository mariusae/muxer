#include "a.h"
#include "muxer.h"
#include <task.h>

char* argv0;
int debug = 0;

void
usage()
{
	fprint(2, "usage: %s [-a announceaddr] [-s statsaddr] [-D] destinations...\n", argv0);
	taskexitall(1);
}

void
taskmain(int argc, char **argv)
{
	int fd, cfd, sfd, aport, dport, port, sport, i;
	char aaddr[64], daddr[64], saddr[64], peer[16];
	
	snprint(aaddr, sizeof aaddr, "*");
	aport = 14041;

	snprint(saddr, sizeof saddr, "*");
	sport = 14040;

	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 'a':
		if(netparse(EARGF(usage()), aaddr, &aport) < 0)
			usage();
		break;
	case 's':
		if(netparse(EARGF(usage()), saddr, &sport) < 0)
			usage();
		break;
	case 'h':
		usage();
	}ARGEND

	if((sfd = netannounce(TCP, saddr, sport)) < 0){
		fprint(2, "announce %s:%d failed: %r\n", saddr, sport);
		taskexitall(1);
	}
	servestatus(sfd);
	
	for(i=0; i<argc; i++){
		if(netparse(argv[i], daddr, &dport) < 0)
			usage();

		sessdial(TCP, daddr, dport, "%s:%d*", daddr, dport);
	}

	if ((fd = netannounce(TCP, aaddr, aport)) < 0){
		fprint(2, "announce %s:%d failed: %r\n", aaddr, aport);
		taskexitall(1);
	}

	while((cfd = netaccept(fd, peer, &port)) >= 0)
		sesscreate(cfd, "%s:%d", peer, port);
}
