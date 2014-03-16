#include "a.h"
#include "muxer.h"
#include <task.h>

static void readsession(Session*);
static void readsessiontask(void *v);
static void writesession(Session*);
static void writesessiontask(void *v);

Session*
mksession(int fd, Channel* r, char *fmt, ...)
{
	Session *s;
	va_list arg;

	s = emalloc(sizeof(Session));
	s->fd = fd;

	va_start(arg, fmt);
	s->label = emalloc(256);
	vsnprintf(s->label, 256, fmt, arg);
	va_end(arg);

	s->r = r;
	s->w = chancreate(sizeof(void*), 256);

	readsession(s);
	writesession(s);

	return s;
}

void
freesession(Session *s)
{
	chansendp(s->w, nil);
	free(s->label);
	free(s);
}

void
readsession(Session *s)
{
	void **args;

	args = emalloc(1*sizeof(void*));
	args[0] = s;

	taskcreate(readsessiontask, args, STACK);
}

static void
readsessiontask(void *v)
{
	/* args: */
		Session *s;

	uchar siz[4];
	uint n;
	Muxmesg *m;

	void **argv = v;

	s = argv[0];
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
		chansendp(s->r, m);
	}

err:
	if(0)
		;
}

void
writesession(Session *s)
{
	void **args;

	args = emalloc(1*sizeof(void*));
	args[0] = s;

	taskcreate(writesessiontask, args, STACK);
}

static void
writesessiontask(void *v)
{
	/* args: */
		Session *s;

	uchar siz[4];
	Muxframe *f;

	void **argv = v;

	s = argv[0];
	free(argv);

	taskname("write %s", s->label);

	for(;;){
		/* Receive a frame, write a frame */
		f = chanrecvp(s->w);
		if(f == nil){
			break;
		}

		taskstate("%s<- frame tag %d size %d\n", s->label, muxtag(f), f->n);
		dprintf("%s\n", taskgetstate());
		U32PUT(siz, f->n);
		fdwrite(s->fd, siz, 4);
		fdwrite(s->fd, f->buf, f->n);
		taskstate("");

		free(f);
	}

	free(s->w);
}
