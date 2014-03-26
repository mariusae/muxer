#include "a.h"
#include "muxer.h"
#include <fcntl.h>
#include <task.h>
#include <sys/uio.h>

Session *nilsess;

static void readtask(void *v);
static void writetask(void *v);
static void writabletask(void *v);

void
sessinit()
{
	nilsess = emalloc(sizeof *nilsess);
	strecpy(nilsess->label, nilsess->label+sizeof nilsess->label, "nil");
	nilsess->fd = open("/dev/null", O_RDWR);
}

Session*
sesscreate(int fd, Channel *read_messages, char *fmt, ...)
{
	Session *s;
	va_list arg;
	void **args;

	s = emalloc(sizeof(Session));
	s->fd = fd;
	s->ok = 1;
	s->read_messages = read_messages;
	s->messages_to_write = chancreate(sizeof(Muxmesg*), 256);
	// TODO: chansize arbitrary, but needs to be large enough to ensure we don't block
	// during shutdown
	s->request_fdwait = chancreate(sizeof(void*), 2);
	s->fd_writable = chancreate(sizeof(void*), 2);

	va_start(arg, fmt);
	vsnprint(s->label, sizeof s->label, fmt, arg);
	va_end(arg);
	
	dprint("new session %s\n", s->label);

	args = emalloc(1*sizeof(void*));
	args[0] = s;
	taskcreate(readtask, args, STACK);

	args = emalloc(1*sizeof(void*));
	args[0] = s;
	taskcreate(writetask, args, STACK);

	args = emalloc(1*sizeof(void*));
	args[0] = s;
	taskcreate(writabletask, args, STACK);

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

/**
 * Output is a Muxmesg, which must be unlocked before this task will send another msg.
 */
static void
readtask(void *v)
{
	/* args: */
		Session *s;

	Muxmesg *m;
	Session **spp, *sp;
	QLock lk;
	session_t* session;
	int read;

	memset(&lk, 0, sizeof lk);

	void **argv = v;

	s = argv[0];
	free(argv);

	session = session_create();

	/* Broken: this will read a ptr off of a nonexistent stack
	 * when we're off. */
	sp = s;
	spp = &sp;

	// reusable Muxmesg: this task waits until the broker unlocks before sending the next
	m = emalloc(sizeof *m);
	m->sp = spp;
	m->locked = &lk;

	stats.nsess++;

	taskname("sessread %s (fd=%d)", s->label, s->fd);

	while(s->ok){
		qlock(&lk);
		m->msg = emalloc(sizeof(mux_msg_t));

		// read while there is not a message available
		while ((read = mux_msg_prepare(m->msg, session)) < 0) {
			dtaskstate("waiting to read");
			fdwait(s->fd, 'r');
			dtaskstate("reading");
			// read the max of the requested amount or the readahead amount
			read = -read < READAHEAD ? READAHEAD : -read;
			buf_t* buf = session_append_alloc(session, read);
			// read, and truncate to the actual read amount
			read = fdread(s->fd, buf->data, buf->size);
			if (read <= 0) {
				goto session_finished;
			}
			buf_trim(buf, 0, read);
		}

		/* Adjust for protocol bugs (TODO: move to mux.c) */
		if(m->msg->type == BAD_Tdiscarded)
			m->msg->type = BAD_Tdiscarded;
		if(m->msg->type == BAD_Rerr)
			m->msg->type = Rerr;

		chansendp(s->read_messages, m);
		dtaskstate("waiting for done signal");
	}

session_finished:

	dprint("session %s died\n", s->label);

	stats.nsess--;


	/**
	 * It's a bit awkward that this has responsibility 
	 * for maintaining the session struct.
	 * Should join the exits of all tasks to free them. Possible in the broker?
	 * Needs an erlang OTP Supervisor one-for-all death strategy?
	 */
	sp = nilsess;
	s->ok = 0;
	chansendp(s->messages_to_write, nil);
	chansendp(s->request_fdwait, nil);
	close(s->fd);
	free(s);
}

/**
 * Input is either a signal that the session's fd is writable, or a mux_msg_t which
 * will be destroyed after being serialized to the session.
 */
static void
writetask(void *v)
{
	/* args: */
		Session *s;

	session_t* session;
	mux_msg_t* msg;
	void* always_nil;
	buf_t* buf;
	uint32_t i;
	struct iovec* iovecs;
	uint32_t bufcount;
	size_t wrote;
	int fdwaiting = 0;
	Alt alts[3];

	void **argv = v;
	s = argv[0];
	free(argv);

	alts[0].c = s->messages_to_write;
	alts[0].op = CHANRCV;
	alts[0].v = &msg;
	alts[1].c = s->fd_writable;
	alts[1].op = CHANRCV;
	alts[1].v = &always_nil;
	alts[2].op = CHANEND;
	session = session_create();

	taskname("sesswrite %s (fd=%d)", s->label, s->fd);

	while(s->ok){
		if (fdwaiting) {
			dtaskstate("waiting for fd to be writable, or for a new message");
		} else {
			dtaskstate("waiting for a new message");
		}

		switch (chanalt(alts)) {
		case 0:
			if (msg == nil) {
				goto finished;
			}
			// message was received
			mux_msg_encode(session, msg);
			mux_msg_destroy(msg);
			break;
		case 1:
			// our fdwait returned
			fdwaiting = 0;
			dtaskstate("writing");

			// execute a writev that attempts to flush the entire session
			bufcount = session_bufcount(session);
			iovecs = emalloc(sizeof(struct iovec) * bufcount);
			for (i = 0; i < bufcount; i++) {
				buf = session_buf(session, session->head + i);
				iovecs[i].iov_base = buf->data;
				iovecs[i].iov_len = buf->size;
			}
			wrote = writev(s->fd, iovecs, bufcount);
			free(iovecs);

			// drop the written data from the session
			assert( wrote < ((uint32_t)1 << 31) );
			session_drop(session, (int)wrote);
			break;
		}

		// if there is data in the session, request an fdwait (doesn't matter
		// what we send, as long as it isn't nil)
		if (!fdwaiting && session_bufcount(session) > 0) {
			chansendp(s->request_fdwait, &s->ok);
			fdwaiting = 1;
		}
	}

finished:
	dtaskstate("exiting");
	session_destroy(session);
}

/**
 * A task that does nothing except fdwait (when requested via a non-nil message on an
 * input channel), and output a writable event on an output channel.
 */
static void
writabletask(void *v)
{
	/* args: */
		Session *s;

	void **argv = v;
	s = argv[0];
	free(argv);

	taskname("sesswritable %s (fd=%d)", s->label, s->fd);

	while(s->ok){
		if (chanrecvp(s->request_fdwait) == nil) break;
		fdwait(s->fd, 'w');
		chansend(s->fd_writable, nil);
	}

	dtaskstate("exiting");
}
