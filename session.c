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
	Channel *donec;

	void **argv = v;

	s = argv[0];
	c = argv[1];
	free(argv);
	
	donec = chancreate(sizeof(void*), 1);

	taskname("read %s", s->label);

	for(;;){
		taskstate("reading frame header");
		if(fdreadn(s->fd, hd, 8) != 8)
			break;

		m = emalloc(sizeof *m);
		m->hd.siz = U32GET(hd);
		m->hd.type = (char)hd[4];
		m->hd.tag = U24GET(hd+5);
		
		/* XXX adjust for  stype == -62 || stype == 127){ */
		
		m->donec = donec;
		m->s = s;

		chansendp(c, m);
		taskstate("waiting for done signal");
		free(chanrecvp(donec));
	}

	chanfree(donec);
}
