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

	uchar siz[4];
	uint n;
	Muxmesg *m;

	void **argv = v;

	s = argv[0];
	c = argv[1];
	free(argv);
	
	taskname("read %s", s->label);


	for(;;){
		/* Read a frame, send a frame */
		taskstate("reading frame size");
		if(fdreadn(s->fd, siz, 4) != 4)
			goto err;

		n = U32GET(siz);

		m = emalloc(sizeof(Muxmesg));
		m->s = s;
		m->f = emalloc(sizeof(Muxframe)+n);
		m->f->n = n;

		taskstate("reading frame");
		if(fdreadn(s->fd, m->f->buf, n) != n){
			free(m->f);
			free(m);
			goto err;
		}

		dprintf("%s-> read frame size %d\n", s->label, n);

		taskstate("sending message");
		chansendp(c, m);
	}

err:
	if(0)
		;
}
