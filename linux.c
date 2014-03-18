#include "a.h"
#include "muxer.h"

#define _GNU_SOURCE
#define __USE_GNU
#include <fcntl.h>

/* Note: may not actually buy us anything; really this is moving buffering
 * from our allocated ones to the kernel */

/* XXX - return errors */
void
copyframe(Session *dst, Session *src, Muxhdr *hd)
{
	int pfd[2];
	uchar hdbuf[8];
	int m, nr, nw, ntot;

	/* XXX: Rerr the dst */
	if(pipe(pfd) < 0){
		fprint(2, "pipe: %r\n");
		sessfatal(src, "%r");
		return;
	}

	fdnoblock(pfd[0]);
	fdnoblock(pfd[1]);

	U32PUT(hdbuf, hd->siz);
	hdbuf[4] = hd->type;
	U24PUT(hdbuf+5, hd->tag);

	if(fdwrite(dst->fd, hdbuf, 8) != 8){
		fprint(2, "fdwrite to %s: %r\n", dst->label);
		sessfatal(dst, "%r");
		sessfatal(src, "dst: %r");
		return;
	}

	dtaskstate("splice %s->%s size %d type %d tag %d", 
		src->label, dst->label, hd->siz, hd->type, hd->tag);

	for(ntot=0; ntot < hd->siz-4; ntot += nr){
		while((nr=splice(src->fd, nil, pfd[1], nil, hd->siz-4-ntot, 
				SPLICE_F_NONBLOCK))<0 && errno == EAGAIN){
			fdwait(src->fd, 'r');
			fdwait(pfd[1], 'w');
		}

		if(nr<0){
			fprint(2, "splice from %s failed: %r\n", src->label);
			sessfatal(src, "splice: %r");
			sessfatal(dst, "splice: %r");
			goto fail;
		}

		for(nw=0; nw<nr; nw+=m){
			while((m=splice(pfd[0], nil, dst->fd, nil, nr-nw, SPLICE_F_NONBLOCK))<0
					&& errno == EAGAIN){
				fdwait(pfd[0], 'r');
				fdwait(dst->fd, 'w');
			}

			if(m<0){
				sessfatal(src, "splice: %r");
				sessfatal(dst, "splice: %r");
				goto fail;
			}
		}
	}

  fail:
	close(pfd[0]);
	close(pfd[1]);
}

