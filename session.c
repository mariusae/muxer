#include "a.h"
#include "muxer.h"
#include <task.h>
#include <fcntl.h>

Session *nilsess;

static void readtask(void *v);

void
sessinit()
{
	nilsess = emalloc(sizeof *nilsess);
	strecpy(nilsess->label, nilsess->label+sizeof nilsess->label, "nil");
	nilsess->fd = open("/dev/null", O_RDWR);
}

Session*
sesscreate(int fd, Channel *c, char *fmt, ...)
{
	Session *s;
	va_list arg;
	void **args;

	s = emalloc(sizeof(Session));
	s->fd = fd;
	s->ok = 1;

	va_start(arg, fmt);
	vsnprint(s->label, sizeof s->label, fmt, arg);
	va_end(arg);
	
	dprint("new session %s\n", s->label);

	args = emalloc(2*sizeof(void*));
	args[0] = s;
	args[1] = c;

	taskcreate(readtask, args, STACK);

	return s;
}

void
sessfatal(Session *s, char *fmt, ...)
{
	char buf[64];
	va_list arg;
	
	if(s == nilsess)
		return;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);

	s->ok = 0;

	dprint("Session %s failed: %s\n", s->label, buf);

	snprint(s->label, sizeof s->label, "%s [failed: %s]", s->label, buf);
}


static void
readtask(void *v)
{
	/* args: */
		Session *s;
		Channel *c;

	uchar hd[8];
	Muxmesg *m;
	Session **spp, *sp;
	QLock lk;

	memset(&lk, 0, sizeof lk);

	void **argv = v;

	s = argv[0];
	c = argv[1];
	free(argv);

	/* Broken: this will read a ptr off of a nonexistent stack
	 * when we're off. */
	sp = s;
	spp = &sp;

	stats.nsess++;

	taskname("sessread %s (fd=%d)", s->label, s->fd);

	while(s->ok){
		qlock(&lk);
		dtaskstate("reading frame header");
		if(fdreadn(s->fd, hd, 8) < 8)
			break;

		m = emalloc(sizeof *m);
		m->hd.siz = U32GET(hd);
		m->hd.type = (char)hd[4];
		m->hd.tag = U24GET(hd+5);
		m->locked = &lk;

		/* Adjust for protocol bugs */
		if(m->hd.type == -62)
			m->hd.type = 66;
		if(m->hd.type == 127)
			m->hd.type = -128;

		m->sp = spp;

		chansendp(c, m);
		dtaskstate("waiting for done signal");
	}

	dprint("session %s died\n", s->label);

	stats.nsess--;

	sp = nilsess;
	s->ok = 0;
	close(s->fd);

	/* It's a bit awkward that this has responsibility 
	 * for maintaining the session struct. */
	free(s);
}
