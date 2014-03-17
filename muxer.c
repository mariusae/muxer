#include "a.h"
#include "muxer.h"
#include <task.h>
#include <fcntl.h>
#include <sys/uio.h>

char* argv0;
int debug = 0;

void brokertask(void *v);
void writeheader(Session *s, Muxhdr *h);
void writeerr(Session *s, uint32 tag, char *fmt, ...);
void writeframe(Session *dst, Muxhdr *hd, char *buf);
void copyframe(Session *dst, Session *src, Muxhdr *hd);

Session nilsess;

void
usage()
{
	fprint(2, "usage: %s [-a announceaddr] [-D] destaddr\n", argv0);
	taskexitall(1);
}

void
taskmain(int argc, char **argv)
{
	int fd, cfd, aport, dport, port;
	char *aaddr, *daddr, peer[16];
	Session *ds, *s;
	Channel *c;
	void **args;

	strcpy(nilsess.label, "nil");
	nilsess.fd = open("/dev/null", O_RDWR);

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
		fprint(2, "dst %s:%d unreachable\n", daddr, dport);
		taskexitall(1);
	}

	ds = mksession(fd, "dst %s:%d", daddr, dport);

	if ((fd = netannounce(TCP, aaddr, aport)) < 0){
		fprint(2, "announce %s:%d failed: %r\n", aaddr, aport);
		taskexitall(1);
	}

	c = chancreate(sizeof(void*), 256);

	args = emalloc(sizeof(void*)*2);
	args[0] = ds;
	args[1] = c;
	taskcreate(brokertask, args, STACK);

	readsession(ds, c);

	while((cfd = netaccept(fd, peer, &port)) >= 0){
		s = mksession(cfd, "client %s:%d", peer, port);
		dprint("new client %s\n", s->label);
		readsession(s, c);
	}
}

void
brokertask(void *v)
{
	/* args: */
		Session *ds;
		Channel *c;
	int tag;
	Muxmesg *mesg, *tmesg;
	void **argv;
	Tags *tags;
	Muxhdr hd;

	argv = v;
	ds = argv[0];
	c = argv[1];
	free(v);

	tags = mktags((1<<24)-1);
	mesg = nil;

	taskname("broker");

	for(;;){
		taskstate("waiting for message");
		mesg = chanrecvp(c);

		if(mesg->hd.tag == 0 || mesg->hd.type == 0){
			copyframe(&nilsess, *mesg->sp, &mesg->hd);
			goto next;
		}

		if(abs(mesg->hd.type) >= 64){
			if(mesg->hd.type > 0){
				writeerr(*mesg->sp, mesg->hd.tag, 
					"Unknown control message %d", mesg->hd.type);
			}

			copyframe(&nilsess, *mesg->sp, &mesg->hd);
			goto next;
		}

		if(mesg->hd.type > 0){	/* T-message */
			if((tag = nexttag(tags, mesg)) < 0){
				copyframe(&nilsess, *mesg->sp, &mesg->hd);
				writeerr(*mesg->sp, mesg->hd.tag, "tags exhausted");
				free(mesg);
				goto next;
			}

			hd = mesg->hd;
			hd.tag = tag;

			copyframe(ds, *mesg->sp, &hd);
		}else{	/* R-message */
			if((tmesg = puttag(tags, mesg->hd.tag)) == nil){
				dprint("no T-message for tag %d\n", mesg->hd.tag);
				copyframe(&nilsess, *mesg->sp, &mesg->hd);
				free(mesg);
				goto next;
			}

			hd = mesg->hd;
			hd.tag = tmesg->hd.tag;

			copyframe(*tmesg->sp, *mesg->sp, &hd);

			free(mesg);
			free(tmesg);
		}
  next:
  		qunlock(mesg->locked);
	}

	freetags(tags);
}

void
writeerr(Session *s, uint32 tag, char *fmt, ...)
{
	va_list arg;
	char buf[64];
	Muxhdr hd;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);

	hd.siz = strlen(buf);
	hd.type = Rerr;
	hd.tag = tag;

	writeframe(s, &hd, buf);
}

void
writeframe(Session *dst, Muxhdr *hd, char *buf)
{
	uchar hdbuf[8];

	U32PUT(hdbuf, hd->siz);
	hdbuf[4] = hd->type;
	U24PUT(hdbuf+5, hd->tag);

	fdwrite(dst->fd, hdbuf, sizeof hdbuf);
	fdwrite(dst->fd, buf, hd->siz);
}

void
copyframe(Session *dst, Session *src, Muxhdr *hd)
{
	uchar hdbuf[8];
	uchar buf[1024];
	struct iovec iov[2], *io, *iop;
	int n, tot, ion;

	U32PUT(hdbuf, hd->siz);
	hdbuf[4] = hd->type;
	U24PUT(hdbuf+5, hd->tag);
	
	iov[0].iov_base = hdbuf;
	iov[0].iov_len = 8;
	
	io = &iov[1];
	iop = iov;
	ion = 2;

	dprint("%s->%s size %d type %d tag %d\n", 
		src->label, dst->label, hd->siz, hd->type, hd->tag);

	taskstate("%s->%s frame tag %d size %d\n", 
		src->label, dst->label, hd->tag, hd->siz);


	for(tot=0; tot<hd->siz-4; tot+=n){
		n = hd->siz-4-tot;
		if(n > sizeof buf)
			n = sizeof buf;

		if((n=fdread(src->fd, buf, n)) < 0){
			/* It might be interesting to explore having some sort of 
			 * trailer to indicate success or failure from middle boxes,
			 * so that these kinds of failures may be handled gracefully,
			 * and the recipient session not destroyed. */
			src->active = 0;
			fprint(2, "Failed to read from session %s: %r", src->label);
			break;
		}

		io->iov_base = buf;
		io->iov_len = n;

		if(fdwritev(dst->fd, iop, ion) < n){
			fprint(2, "Failed to write to session %s: %r", dst->label);
			dst = &nilsess;
		}
		
		iop = io;
		ion = 1;
	}

	taskstate("");
}

/* Todo: sendframe */
