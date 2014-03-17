#include "a.h"
#include "muxer.h"
#include <task.h>

static void readsessiontask(void *v);

Session*
mksession(int fd, char *fmt, ...)
{
	Session *s;
	va_list arg;

	s = emalloc(sizeof(Session));
	s->fd = fd;

	va_start(arg, fmt);
	vsnprint(s->label, sizeof s->label, fmt, arg);
	va_end(arg);
	
	s->active = 1;

	return s;
}

void
readsession(Session *s, Channel *c)
{
	void **args;

	args = emalloc(2*sizeof(void*));
	args[0] = s;
	args[1] = c;

	taskcreate(readsessiontask, args, STACK);
}

static void
readsessiontask(void *v)
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

	sp = s;
	spp = &sp;

	taskname("read %s", s->label);

	while(s->active){
		qlock(&lk);
		taskstate("reading frame header");
		if(fdreadn(s->fd, hd, 8) != 8)
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
		taskstate("waiting for done signal");
	}

	sp = &nilsess;
	s->active = 0;
	close(s->fd);

	/* Is this really our responsibility? */
	free(s);
}
