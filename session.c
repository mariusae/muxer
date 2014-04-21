#include "a.h"
#include "muxer.h"
#include <task.h>
#include <fcntl.h>

static void brokertask(void *v);
static void dialtask(void *v);
int dial(Session *s);

static Muxmesg* Mread(Session *s);
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
	s->rnotify = chancreate(sizeof(unsigned long), 2);
	s->wnotify = chancreate(sizeof(unsigned long), 2);
	s->tags = mktags(1024);

	dprint("%s: create fd\n", s->label);

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
	s->rnotify = chancreate(sizeof(unsigned long), 2);
	s->wnotify = chancreate(sizeof(unsigned long), 2);
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
	Muxmesg *mread;
	Muxmesg *mtowrite;
	unsigned long unused;
	int writewaiting;
	Alt alts[3];

	s = v;

begin:
	taskname("%s broker", s->label);

	stats.nactivesess++;
	stats.nlifetimesess++;
	
	alts[0].c = s->rnotify;
	alts[0].v = &unused;
	alts[0].op = CHANRCV;
	alts[1].op = CHANRCV;
	alts[2].op = CHANEND;

// we always alt for `rnotify`, but depending on whether we have an outbound message,
// WWAIT and CWAIT will toggle between alting for `wnotify` and `callc` respectively
// TODO: this can all be replaced by the session buffering
#define WWAIT() do {	\
	alts[1].c = s->wnotify;	\
	alts[1].v = &unused;	\
	writewaiting = 1;	\
	fdnotify(s->fd, 'w', s->wnotify);	\
} while(0)

#define CWAIT() do {	\
	alts[1].c = s->callc;	\
	alts[1].v = &mtowrite;	\
	writewaiting = 0;	\
} while(0)

	// begin the (indefinite) fdnotify for rnotify, and initially wait for calls
	printf("requesting rnotify\n");
	fdnotify(s->fd, 'r', s->rnotify);
	CWAIT();

	for(;;){
		if (writewaiting) {
			dtaskstate("read||write");
		} else {
			dtaskstate("read||call");
		}
		switch(chanalt(alts)){
		case 0:
			dtaskstate("reading");
			// TODO: blocking/fdwaiting read
			mread = Mread(s);
			if(mread == nil)
				goto hangup;

			dprint("%s: %c read\n", s->label, mread->hd.type>0?'T':'R');

			if(abs(mread->hd.type) >= 64)
				Mwriteerr(s, mread->hd.tag, "Unknown control message %d", mread->hd.type);
			else if(mread->hd.type > 0)
				Trecv(s, mread);
			else
				Rrecv(s, mread);

			// resume fdnotify for reads
			fdnotify(s->fd, 'r', s->rnotify);
			break;

		case 1:
			if (writewaiting) {
				// fd is writable: execute call
				dprint("%s: %c writing\n", s->label, mtowrite->hd.type>0?'T':'R');

				/* Fix up these protocol bugs somewhere. */
				if(mtowrite->hd.type > 0 && mtowrite->hd.type != Rerr)
					Tcall(s, mtowrite);
				else
					Rcall(s, mtowrite);

				// message flushed: begin waiting for calls again
				CWAIT();
			} else {
				// received a call: buffer it in-place and wait to become writable
				dprint("%s: %c call\n", s->label, mtowrite->hd.type>0?'T':'R');
				WWAIT();
			}

			break;
		}
	}

hangup:
	dtaskstate("hangup");

	/* Save our precious file descriptors. */
	close(s->fd);

	routedel(s);

	 while((mtowrite=putnexttag(s->tags)) != nil){
		chansendp(mtowrite->replyc, Merr(mtowrite->hd.tag, "dest hangup"));
		free(mtowrite);
	 }
	 
	 while(s->npending-- > 0)
	 	chanrecvp(s->callc);

	stats.nactivesess--;

	if(s->type == Sessdial && dial(s) == 0){
		routeadd(s);
		goto begin;
	}

	freetags(s->tags);
	chanfree(s->callc);
	// TODO: need the ability to cancel fdnotify in order to handle freeing these safely
	// chanfree(s->rnotify);
	// chanfree(s->wnotify);
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

/*
 * Executes blocking/fdwaiting reads to fetch a Muxmesg for the given session.
 * If this method returns nil, the session has already been marked !ok.
 *
 * TODO: need to incorporate session_t to eliminate the fdwaits here
 */
static Muxmesg*
Mread(Session *s)
{
	uchar hd[8];
	Muxmesg *m;
	int n;

	dtaskstate("reading frame header");

	if((n=fdreadn(s->fd, hd, 8)) < 0){
		sessfatal(s, "%r while reading header");
		return nil;
	} else if(n!=8){
		sessfatal(s, "EOF while reading header");
		return nil;
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
		free(m);
		return nil;
	}

	return m;
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
