#include "a.h"
#include "muxtask.h"
#include <mux.h>
#include <task.h>

/* XXX very leaky! */

enum
{
	STACK = 32768,
	Maxtags = 200,
	Maxsessions = 200,
};


typedef
struct Session
{
	char *label;
	Channel *rc;
	Channel *wc;
} Session;

typedef
struct Call
{
	Session *s;
} Call;

char* argv0;
int debug = 0;

void brokertask(void *v);
void readtask(void *v);
void writetask(void *v);
Session* mksession(char *label);
void freesession(Session *s);
Session* servesession(int fd, char *label);

void
usage()
{
	fprintf(stderr, "usage: %s \n", argv0);
	taskexitall(1);
}

void
taskmain(int argc, char **argv)
{
	int lport = 14041, fd, cfd, rport, p;
	char *s;
	char remote[16];
	char label[32];
	void **args;
	Session *ds = nil;
	Channel *nc;

	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 'l':
		lport = atoi(EARGF(usage()));
		break;
	case 'd':
		s = EARGF(usage());
		p = netmunge(s);
		if(p < 0)
			usage();

		if ((fd = netdial(TCP, s, p)) < 0){
			fprintf(stderr, "dst %s:%d unreachable\n", s, p);
			taskexitall(1);
		}

		snprintf(label, sizeof label, "dest %s:%d", s, p);
		ds = servesession(fd, label);

		break;
	case 'h':
		usage();
	}ARGEND

	if(ds == nil){
		fprintf(stderr, "no destination\n");
		usage();
	}

	nc = chancreate(sizeof(void*), 1);

	args = emalloc(sizeof(void*)*2);
	args[0] = ds;
	args[1] = nc;

	taskcreate(brokertask, args, STACK);

	if ((fd = netannounce(TCP, 0, lport)) < 0){
		fprintf(stderr, "announce port %d failed: %s\n", lport, strerror(errno));
		taskexitall(1);
	}
	fdnoblock(fd);

	while((cfd = netaccept(fd, remote, &rport)) >= 0){
		snprintf(label, sizeof label, "client %s:%d", remote, rport);
		if(debug)
			fprintf(stderr, "new client %s\n", label);
		chansendp(nc, servesession(cfd, label));
	}
}

void
brokertask(void *v)
{
	/* args: */
		Session *ds;
		Channel *nc;
	int n = 0, i, tag;
	Alt alts[Maxsessions+2];
	Session *sessions[Maxsessions];
	Session *pending[Maxtags];
	int tagmap[Maxtags];
	mux_frame_t *f;
	mux_msg_t *m;
	int decode_result;
	void *p, **args;
	Session *s;

	void **argv = v;

	m = emalloc(sizeof(mux_msg_t));
	ds = argv[0];
	nc = argv[1];
	free(p);

	for(i=0; i < nelem(tagmap); i++)
		tagmap[i] = -1;

	for(;;){
		alts[0].c = ds->rc;
		alts[0].op = CHANRCV;
		alts[0].v = &f;
		alts[1].c = nc;
		alts[1].op = CHANRCV;
		alts[1].v = &p;

		for(i=0; i<n; i++){
			alts[2+i].c = sessions[i]->rc;
			alts[2+i].op = CHANRCV;
			alts[2+i].v = &f;
		}
		alts[2+i].op = CHANEND;

		if(0)
		for(i=0; alts[i].op != CHANEND; i++)
			fprintf(stderr, "alt[%d] c=%p op=%d\n", i, alts[i].c, alts[i].op);

		switch((i = chanalt(alts))){
		case 0:
			decode_result = mux_msg_decode(m, f);
			// TODO: cannot free the frame here because it is shared with the message
			free(f);

			if(decode_result < 0){
				if(debug)
					fprintf(stderr, "[%s] decode failed: %s\n", ds->label, m->msg.exception);
				continue;
			}
			if(m->tag == 0){
				// marker message
				continue;
			}

			s = pending[m->tag];
			if(s==nil){
				if(debug)
					fprintf(stderr, "[%s] unknown tag %d\n", ds->label, m->tag);
				continue;
			}
			tag = tagmap[m->tag];

			if(debug)
				fprintf(stderr, "[%s] tag %d->%d\n", s->label, m->tag, tag);

			tagmap[m->tag] = -1;
			m->tag = tag;

			// TODO: reuse frames
			f = mux_frame_create(128);
			mux_msg_encode(f, m);
			chansendp(s->wc, f);

			continue;

		case 1:
			if(debug)
				fprintf(stderr, "newsession %d\n", n);
			sessions[n++] = p;
			continue;
		}

		s = sessions[i-2];

		for(tag=1; tag < nelem(tagmap) && tagmap[tag] > -1; tag++);
		if(tag == nelem(tagmap))
			continue; /* XXX */

		m = muxF2M(f);
		free(f);
		if(m == nil)
			continue;
		if(m->tag == 0){
			free(m);
			continue;
		}

		if(debug)
			fprintf(stderr, "[%s] tag=%d->%d\n", s->label, m->tag, tag);

		tagmap[tag] = m->tag;
		pending[tag] = s;

		m->tag = tag;
		f = muxM2F(m);
		free(m);

		chansendp(ds->wc, f);
	}
}

Session*
servesession(int fd, char *label)
{
	void **args;
	Session *s = mksession(label);

	args = emalloc(2*sizeof(void*));
	args[0] = (void*)(uintptr)fd;
	args[1] = s;

	taskcreate(readtask, args, STACK);

	args = emalloc(2*sizeof(void*));
	args[0] = (void*)(uintptr)fd;
	args[1] = s;

	taskcreate(writetask, args, STACK);

	return s;
}

void
readtask(void *v)
{
	/* args: */
		int fd;
		Session *s;
	void **argv = (void**)v;
	mux_frame_t *f;

	fd = (uintptr)argv[0];
	s = argv[1];
	free(v);

	while((f=muxreadframe(fd)) != nil){
		if(debug)
			fprintf(stderr, "[%s] R %d \n", s->label, f->size);
		chansendp(s->rc, f);
	}

	/* XXX shutdown/close */

}

void
writetask(void *v)
{
	/* args: */
		int fd;
		Session *s;
	void **argv = (void**)v;
	mux_frame_t *f;

	fd = (uintptr)argv[0];
	s = argv[1];
	free(v);

	while((f=chanrecvp(s->wc)) != nil){
		if(debug)
			fprintf(stderr, "[%s] W %d\n", s->label, f->size);
		fdwrite(fd, f->data, f->size);
		free(f);
	}

	/* XXX shutdown/close */
}

Session*
mksession(char *label)
{
	Session *s;
	
	s = emalloc(sizeof(Session));

	s->label = strdup(label);
	s->rc = chancreate(sizeof(void*), 64);
	s->wc = chancreate(sizeof(void*), 64);

	return s;
}

void
freesession(Session *s)
{
	chanfree(s->rc);
	chanfree(s->wc);
	free(s);
}
