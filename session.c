#include "a.h"
#include "muxer.h"
#include <task.h>
#include <fcntl.h>

static void readtask(void *v);
static void brokertask(void *v);
static void dialtask(void *v);
int dial(Session *s);

static void Mwrite(Session *s, Muxmesg *m);
static void Mwriteerr(Session *s, uint32 tag, char *fmt, ...);
static Muxmesg* Mverr(uint32 tag, char *fmt, va_list arg);
static Muxmesg* Merr(uint32 tag, char *fmt, ...);

static void Trecv(Session *s, Muxmesg *m);
static void Rcall(Session *s, Muxmesg *m);

static void Tcall(Session *s, Muxmesg *m);
static void Rrecv(Session *s, Muxmesg *m);

enum 
{
	Maxredialdelay = 10000
};

Session*
sesscreate(int fd, char *fmt, ...)
{
	Session *s;
	va_list arg;

	s = emalloc(sizeof(Session));
	s->fd = fd;
	s->ok = 1;
	s->type = Sessfd;

	va_start(arg, fmt);
	vsnprint(s->label, sizeof s->label, fmt, arg);
	va_end(arg);
	
	s->callc = chancreate(sizeof(void*), 8);
	s->readc = chancreate(sizeof(void*), 8);
	s->tags = mktags(1024);

	dprint("%s: create fd\n", s->label);

	taskcreate(readtask, s, STACK);
	taskcreate(brokertask, s, STACK);

	return s;
}

Session*
sessdial(int net, char *host, int port, char *fmt, ...)
{
	Session *s;
	va_list arg;

	s = emalloc(sizeof(Session));
	s->type = Sessdial;
	s->dialnet = net;
	s->dialhost = estrdup(host);
	s->dialport = port;

	va_start(arg, fmt);
	vsnprint(s->label, sizeof s->label, fmt, arg);
	va_end(arg);

	s->callc = chancreate(sizeof(void*), 8);
	s->readc = chancreate(sizeof(void*), 8);
	s->tags = mktags(1024);

	dprint("%s: create dial\n", s->label);

	taskcreate(dialtask, s, STACK);

	return s;
}

void
sessfatal(Session *s, char *fmt, ...)
{
	char buf[64];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);

	s->ok = 0;

	dprint("session %s failed: %s\n", s->label, buf);
}

void
dialtask(void *v)
{
	dial(v);

	taskcreate(readtask, v, STACK);
	taskcreate(brokertask, v, STACK);
	routeadd(v);
}

int
dial(Session *s)
{
	uint delay, n;

	delay = 10;
	n = 2;

	assert(s->type == Sessdial);

	taskname("%s: dialler", s->label);
	dtaskstate("attempt #1");
	while((s->fd = netdial(s->dialnet, s->dialhost, s->dialport)) < 0){
		stats.nredials++;
		dprint("failed to dial: %r - redialling in %dms\n", delay);
		taskdelay(delay);
		if((delay *= 2) > Maxredialdelay)
			delay = Maxredialdelay;
		dtaskstate("attempt #%d", n++);
	}
	
	s->ok = 1;

	return 0;
}

static void
brokertask(void *v)
{
	Session *s;
	Muxmesg *m;
	Alt alts[3];

	s = v;

begin:
	taskname("%s broker", s->label);

	stats.nactivesess++;
	stats.nlifetimesess++;
	

	alts[0].c = s->readc;
	alts[0].v = &m;
	alts[0].op = CHANRCV;
	alts[1].c = s->callc;
	alts[1].v = &m;
	alts[1].op = CHANRCV;
	alts[2].op = CHANEND;

	for(;;){
		dtaskstate("read||call");
		switch(chanalt(alts)){
		case 0:
			if(m == nil)
				goto hangup;

			dprint("%s: %c read\n", s->label, m->hd.type>0?'T':'R');

			if(abs(m->hd.type) >= 64)
				Mwriteerr(s, m->hd.tag, "Unknown control message %d", m->hd.type);
			else if(m->hd.type > 0)
				Trecv(s, m);
			else
				Rrecv(s, m);

			break;

		case 1:
			dprint("%s: %c call\n", s->label, m->hd.type>0?'T':'R');
			
			/* Fix up these protocol bugs somewhere. */
			if(m->hd.type > 0 && m->hd.type != Rerr)
				Tcall(s, m);
			else
				Rcall(s, m);

			break;
		}
	}

hangup:
	dtaskstate("hangup");

	/* Save our precious file descriptors. */
	close(s->fd);

	routedel(s);

	 while((m=putnexttag(s->tags)) != nil){
		chansendp(m->replyc, Merr(m->hd.tag, "dest hangup"));
		free(m);
	 }
	 
	 while(s->npending-- > 0)
	 	chanrecvp(s->callc);

	stats.nactivesess--;

	if(s->type == Sessdial && dial(s) == 0){
		routeadd(s);
		taskcreate(readtask, s, STACK);
		goto begin;
	}

	freetags(s->tags);
	chanfree(s->callc);
	chanfree(s->readc);
	free(s);
}

static void
Trecv(Session *s, Muxmesg *m)
{
	Session *ds;

	if((ds = routelookup()) == nil){
		/* This probably deserves a first-class application error */
		dprint("no route for message\n");
		Mwriteerr(s, m->hd.tag, "no route for destination");
		return;
	}
	
	m->replyc = s->callc;
	s->npending++;
	stats.nreq++;

	chansendp(ds->callc, m);
}

static void
Rrecv(Session *s, Muxmesg *m)
{
	Muxmesg *savem;

	if((savem=puttag(s->tags, m->hd.tag)) == nil){
		dprint("%s: no T-message for tag %d\n", s->label, m->hd.tag);
		return;
	}

	m->hd.tag = savem->savetag;
	chansendp(savem->replyc, m);
	free(savem);
}

static void
Tcall(Session *s, Muxmesg *m)
{
	int tag;

	if(m->replyc == nil) abort();

	if((tag=nexttag(s->tags, m)) < 0){
		chansendp(m->replyc, Merr(m->hd.tag, "muxer tags exhausted"));
		free(m);
		return;
	}

	m->savetag = m->hd.tag;
	m->hd.tag = tag;
	Mwrite(s, m);
}

static void
Rcall(Session *s, Muxmesg *m)
{
	Mwrite(s, m);
	free(m);
	s->npending--;
}

static void
readtask(void *v)
{
	Session *s;
	uchar hd[8];
	Muxmesg *m;
	int n;

	s = v;

	taskname("%s read", s->label);

	while(s->ok){
		dtaskstate("reading frame header");

		if((n=fdreadn(s->fd, hd, 8)) < 0){
			sessfatal(s, "%r while reading header");
			break;
		} else if(n!=8){
			sessfatal(s, "EOF while readin gheader");
			break;
		}

		n = U32GET(hd)-4;
		m = emalloc(sizeof *m + n);
		m->hd.siz = n;
		m->hd.type = (char)hd[4];
		m->hd.tag = U24GET(hd+5);
		m->replyc = nil;

/* Adjust for protocol bugs
		if(m->hd.type == -62)
			m->hd.type = 66;
		if(m->hd.type == 127)
			m->hd.type = -128;
*/

		dtaskstate("reading frame body");
		if(fdreadn(s->fd, m->body, n) < n){
			sessfatal(s, "while reading body: %r");
			break;
		}

		chansendp(s->readc, m);
	}

	chansendp(s->readc, nil);
}


static void 
Mwrite(Session *s, Muxmesg *m)
{
	uchar hdbuf[8];
	struct iovec iov[2];

	U32PUT(hdbuf, m->hd.siz+4);
	hdbuf[4] = m->hd.type;
	U24PUT(hdbuf+5, m->hd.tag);

	iov[0].iov_base = hdbuf;
	iov[0].iov_len = 8;
	iov[1].iov_base = m->body;
	iov[1].iov_len = m->hd.siz;

	fdwritev(s->fd, iov, 2);
}

static Muxmesg*
Mverr(uint32 tag, char *fmt, va_list arg)
{
	Muxmesg *m;

	m = emalloc(sizeof *m+64);
	vsnprint((char*)m->body, 64, fmt, arg);
	m->hd.siz = strlen((char*)m->body)+4;
	m->hd.type = Rerr;
	m->hd.tag = tag;

	return m;
}


static Muxmesg* 
Merr(uint32 tag, char *fmt, ...)
{
	va_list arg;
	Muxmesg *m;

	va_start(arg, fmt);
	m = Mverr(tag, fmt, arg);
	va_end(arg);

	return m;
}

static void 
Mwriteerr(Session *s, uint32 tag, char *fmt, ...)
{
	va_list arg;
	Muxmesg *m;
	
	va_start(arg, fmt);
	m = Mverr(tag, fmt, arg);
	va_end(arg);
	
	Mwrite(s, m);
	free(m);
}