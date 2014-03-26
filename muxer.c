#include "a.h"
#include "muxer.h"
#include <task.h>

char* argv0;
int debug = 0;

void brokertask(void *v);
void writeerr(Session *dst, mux_msg_t* msg, char *fmt, ...);
void writemsg(Session *dst, mux_msg_t* msg);

void
usage()
{
	fprint(2, "usage: %s [-a announceaddr] [-s statsaddr] [-D] destaddr\n", argv0);
	taskexitall(1);
}

void
taskmain(int argc, char **argv)
{
	int fd, cfd, sfd, aport, dport, port, sport;
	char *aaddr, *daddr, *saddr, peer[16];
	Session *ds;
	Channel *c;
	void **args;
	
	sessinit();

	ds = nil;
	aaddr = "*";
	aport = 14041;
	
	saddr = "*";
	sport = 14040;

	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 'a':
		aaddr = EARGF(usage());
		if((aport = netmunge(&aaddr)) < 0)
			usage();
		break;
	case 's':
		saddr = EARGF(usage());
		if((sport = netmunge(&saddr)) < 0)
			usage();
		break;
	case 'h':
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	daddr = argv[0];
	if((dport = netmunge(&daddr)) < 0)
		usage();
		
	if((sfd = netannounce(TCP, saddr, sport)) < 0){
		fprint(2, "announce %s:%d failed: %r\n", saddr, sport);
		taskexitall(1);
	}
	servestatus(sfd);

	if ((fd = netdial(TCP, daddr, dport)) < 0){
		fprint(2, "dst %s:%d unreachable\n", daddr, dport);
		taskexitall(1);
	}
	c = chancreate(sizeof(void*), 256);
	ds = sesscreate(fd, c, "dst %s:%d", daddr, dport);
	
	if ((fd = netannounce(TCP, aaddr, aport)) < 0){
		fprint(2, "announce %s:%d failed: %r\n", aaddr, aport);
		taskexitall(1);
	}

	args = emalloc(sizeof(void*)*2);
	args[0] = ds;
	args[1] = c;
	taskcreate(brokertask, args, STACK);

	while((cfd = netaccept(fd, peer, &port)) >= 0)
		sesscreate(cfd, c, "client %s:%d", peer, port);
}

void
brokertask(void *v)
{
	/* args: */
		Session *ds;
		Channel *c;
	int tag;
	Muxmesg *mesg;
	Tmesg *tmesg;
	void **argv;
	Tags *tags;

	argv = v;
	ds = argv[0];
	c = argv[1];
	free(v);

	tags = mktags((1<<24)-1);
	mesg = nil;

	taskname("broker");

	for(;;){
		dtaskstate("waiting for message");
		mesg = chanrecvp(c);

		if(mesg->msg->tag == 0 || mesg->msg->type == 0){
      // hm, what?
      assert( -1 );
			goto next;
		}

		if(abs(mesg->msg->type) >= 64){
			if(mesg->msg->type > 0){
				writeerr(*mesg->sp, mesg->msg, 
					"Unknown control message %d", mesg->msg->type);
			} else {
        // ignored
        mux_msg_destroy(mesg->msg);
      }
			goto next;
		}

		if(mesg->msg->type > 0){	/* T-message */
      tmesg = emalloc(sizeof(Tmesg));
      tmesg->tag = mesg->msg->tag;
      tmesg->sp = mesg->sp;
			if((tag = nexttag(tags, tmesg)) < 0){
        free(tmesg);
				writeerr(*mesg->sp, mesg->msg, "tags exhausted");
        goto next;
			}

			mesg->msg->tag = tag;

			/* XXX check sessions ok */
			writemsg(ds, mesg->msg);
      goto next;
		}else{	/* R-message */
			if((tmesg = puttag(tags, mesg->msg->tag)) == nil){
				dprint("no T-message for tag %d\n", mesg->msg->tag);
        mux_msg_destroy(mesg->msg);
				goto next;
			}

			/* XXX check sessions ok */
			mesg->msg->tag = tmesg->tag;
      writemsg(*tmesg->sp, mesg->msg);
			free(tmesg);

			stats.nreq++;
		}

  next:
		if(mesg != nil){
			qunlock(mesg->locked);
      mesg = nil;
		}
	}

	freetags(tags);
}

/**
 * Resets the given msg (with a valid tag) as an error, and sends it on the
 * given Session.
 */
void
writeerr(Session *s, mux_msg_t* msg, char *fmt, ...)
{
	va_list arg;
  rerr_t* rerr;

  mux_msg_reset(msg);
  msg->type = Rerr;
  rerr = &msg->msg.rerr;
  buf_alloc(&rerr->error, 128);

	va_start(arg, fmt);
	vsnprint((char*)rerr->error.data, rerr->error.size, fmt, arg);
	va_end(arg);

	writemsg(s, msg);
}

void
writemsg(Session *s, mux_msg_t* msg)
{
  chansendp(s->messages_to_write, msg);
}
