#include "a.h"
#include "muxer.h"
#include <task.h>

char* argv0;
int debug = 0;

void brokertask(void *v);
void writeframe(Session *s, Muxframe *f);
void writeerr(Session *s, uint32 tag, char *fmt, ...);

void
usage()
{
	fprintf(stderr, "usage: %s [-a announceaddr] [-D] destaddr\n", argv0);
	taskexitall(1);
}

void
taskmain(int argc, char **argv)
{
	int fd, cfd, aport, dport, port;
	char *aaddr, *daddr, peer[16];
	Session *ds, *s;
	Channel *r;
	void **args;

	ds = nil;
	aaddr = "*";
	aport = 14041;

	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 'a':
		aaddr = EARGF(usage());
		if((aport = netmunge(&aaddr)) < 0)
			usage();
		break;
	case 'h':
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	daddr = argv[0];
	if((dport = netmunge(&daddr)) < 0)
		usage();

	if ((fd = netdial(TCP, daddr, dport)) < 0){
		fprintf(stderr, "dst %s:%d unreachable\n", daddr, dport);
		taskexitall(1);
	}

	r = chancreate(sizeof(void*), 256);
	ds = mksession(fd, r, "dst %s:%d", daddr, dport);

	if ((fd = netannounce(TCP, aaddr, aport)) < 0){
		fprintf(stderr, "announce %s %d failed: %s\n", aaddr, aport, strerror(errno));
		taskexitall(1);
	}

	args = emalloc(sizeof(void*)*2);
	args[0] = ds;
	args[1] = r;
	taskcreate(brokertask, args, STACK);

	while((cfd = netaccept(fd, peer, &port)) >= 0){
		s = mksession(cfd, r, "client %s:%d", peer, port);
		dprintf("new %s\n", s->label);
	}
}

void
brokertask(void *v)
{
	/* args: */
		Session *ds;
		Channel *r;
	int stag, dtag, stype;
	Muxmesg *sm, *m1;
	void **argv;
	Session *s;
	Tags *tags;

	argv = v;
	ds = argv[0];
	r = argv[1];
	free(v);

	tags = mktags((1<<24)-1);

	taskname("broker");

	sm = m1 = nil;

	for(;;){
		taskstate("waiting for message");
		sm = chanrecvp(r);

		if((stag = muxtag(sm->f)) == 0 || (stype = muxtype(sm->f)) == 0){
			free(sm->f);
			free(sm);
			continue;
		}
		
		/* There's a bug in in Finagle's mux implementation
		 * since bytes are encoded in two's complement. */
/*		if((stype&0x40) == 0x40){*/
		if(stype<-60 || stype > 63){
			if(stype > 0)
				writeerr(sm->s, stag, "Unknown control message %d", stype);
			free(sm->f);
			free(sm);
			continue;			
		}

		if(stype > 0){	/* T-message */
			if((dtag = nexttag(tags, sm)) < 0){
				writeerr(sm->s, stag, "tags exhausted");
				free(sm->f);
				free(sm);
				continue;
			}

			sm->origtag = stag;

			dprintf("%s-> tag %d->%d\n", sm->s->label, stag, dtag);
			muxsettag(sm->f, dtag);
			chansendp(ds->w, sm->f);

			sm->f = nil;
		}else{	/* R-message */
			if(stag < 0)
				goto skip_frame;	/* Rerr */

			m1 = puttag(tags, stag);
			if(m1 == nil){
				dprintf("unknown tag %d\n", stag);
				goto skip_frame;
			}

			dtag = m1->origtag;
			s = m1->s;

			muxsettag(sm->f, dtag);

			chansendp(s->w, sm->f);
			goto next;

		skip_frame:
			free(sm->f);
		next:
			free(sm);
			free(m1);
		}

	}
	
	freetags(tags);
}

void
writeerr(Session *s, uint32 tag, char *fmt, ...)
{
	Muxframe *f;
	va_list arg;

	f = alloca(sizeof(Muxframe)+260);
	muxsettype(f, Rerr);
	muxsettag(f, tag);
	f->n = 260;
	va_start(arg, fmt);
	f->n = 4 + vsnprintf((char*)f->buf+4, f->n-4, fmt, arg);
	va_end(arg);

	chansendp(s->w, f);
}
