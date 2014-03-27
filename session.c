#include "a.h"
#include "muxer.h"
#include <task.h>
#include <fcntl.h>

static void readtask(void *v);
static void brokertask(void *v);
static void writemesg(Session *s, Muxhdr *hd, uchar *buf);
static void writeerr(Session *s, uint32 tag, char *fmt, ...);
Muxmesg* mkerr(uint32 tag, char *fmt, ...);
static void recvt(Session *s, Muxmesg *m);
static void recvr(Session *s, Muxmesg *m);
static void callt(Session *s, Muxmesg *m);
static void callr(Session *s, Muxmesg *m);

Session*
sesscreate(int fd, char *fmt, ...)
{
	Session *s;
	va_list arg;

	s = emalloc(sizeof(Session));
	s->fd = fd;
	s->ok = 1;
	s->npending = 0;

	va_start(arg, fmt);
	vsnprint(s->label, sizeof s->label, fmt, arg);
	va_end(arg);
	
	s->callc = chancreate(sizeof(void*), 8);
	s->readc = chancreate(sizeof(void*), 8);
	s->tags = mktags(1024);

	dprint("%s: create\n", s->label);

	taskcreate(readtask, s, STACK);
	taskcreate(brokertask, s, STACK);

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

	snprint(s->label, sizeof s->label, "%s [failed: %s]", s->label, buf);
}

static void
brokertask(void *v)
{
	Session *s;
	Muxmesg *m;
	Alt alts[3];

	s = v;

	stats.nactivesess++;
	stats.nlifetimesess++;
	
	taskname("%s broker", s->label);

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
				writeerr(s, m->hd.tag, "Unknown control message %d", m->hd.type);
			else if(m->hd.type > 0)
				recvt(s, m);
			else
				recvr(s, m);

			break;

		case 1:
			dprint("%s: %c call\n", s->label, m->hd.type>0?'T':'R');
	
			if(m->hd.type > 0)
				callt(s, m);
			else
				callr(s, m);

			break;
		}
	}

hangup:
	dtaskstate("hangup");
	snprint(s->label, sizeof s->label, "%s: hangup", s->label);

	routedel(s);

	 while((m=putnexttag(s->tags)) != nil){
		chansendp(m->replyc, mkerr(m->hd.tag, "dest hangup"));
		free(m);
	 }
	 
	 while(s->npending-- > 0)
	 	chanrecvp(s->callc);

	stats.nactivesess--;
	
	freetags(s->tags);
	chanfree(s->callc);
	chanfree(s->readc);
	close(s->fd);
	free(s);
}

static void
recvt(Session *s, Muxmesg *m)
{
	Session *ds;

	if((ds = routelookup()) == nil){
		/* This probably deserves a first-class application error */
		dprint("no route for message\n");
		writeerr(s, m->hd.tag, "Cannot route message");
		return;
	}
	
	m->replyc = s->callc;
	s->npending++;
	stats.nreq++;

	chansendp(ds->callc, m);
}

static void
recvr(Session *s, Muxmesg *m)
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
callt(Session *s, Muxmesg *m)
{
	int tag;
	Muxmesg *errm;

	if((tag=nexttag(s->tags, m)) < 0){
		errm = mkerr(m->hd.tag, "muxer tags exhausted");
		chansendp(m->replyc, errm);
		free(m);
		return;
	}

	m->savetag = m->hd.tag;
	m->hd.tag = tag;
	writemesg(s, &m->hd, m->body);
}

static void
callr(Session *s, Muxmesg *m)
{
	writemesg(s, &m->hd, m->body);
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
			sessfatal(s, "while reading header: %r");
			break;
		} else if(n!=8){
			sessfatal(s, "EOF");
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
writemesg(Session *s, Muxhdr *hd, uchar *buf)
{
	uchar hdbuf[8];

	U32PUT(hdbuf, hd->siz+4);
	hdbuf[4] = hd->type;
	U24PUT(hdbuf+5, hd->tag);

	/* XXX: fdwritev */

	fdwrite(s->fd, hdbuf, 8);
	fdwrite(s->fd, buf, hd->siz);
}

static void
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

	writemesg(s, &hd, (uchar*)buf);
}

Muxmesg*
mkerr(uint32 tag, char *fmt, ...)
{
	va_list arg;
	Muxmesg *m;
	
	m = emalloc(sizeof *m+64);

	va_start(arg, fmt);
	vsnprint((char*)m->body, 64, fmt, arg);
	va_end(arg);

	m->hd.siz = strlen((char*)m->body)+4;
	m->hd.type = Rerr;
	m->hd.tag = tag;
	
	return m;
}