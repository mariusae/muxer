#include "a.h"
#include "muxer.h"
#include <sys/uio.h>

void
copyframe(Session *dst, Session *src, Muxhdr *hd)
{
	uchar hdbuf[8];
	uchar buf[1024];
	struct iovec iov[2], *io, *iop;
	int n, tot, ion;

	U32PUT(hdbuf, hd->siz);
	hdbuf[4] = hd->type;
	U24PUT(hdbuf+5, hd->tag);

	iov[0].iov_base = hdbuf;
	iov[0].iov_len = 8;

	io = &iov[1];
	iop = iov;
	ion = 2;

	dtaskstate("%s->%s frame tag %d size %d\n", 
		src->label, dst->label, hd->tag, hd->siz);

	for(tot=0; tot<hd->siz-4; tot+=n){
		n = hd->siz-4-tot;
		if(n > sizeof buf)
			n = sizeof buf;

		if((n=fdread(src->fd, buf, n)) < 0){
			/* It might be interesting to explore having some sort of 
			 * trailer to indicate success or failure from middle boxes,
			 * so that these kinds of failures may be handled gracefully,
			 * and the recipient session not destroyed. */
			src->active = 0;
			fprint(2, "Failed to read from session %s: %r", src->label);
			break;
		}

		io->iov_base = buf;
		io->iov_len = n;

		if(fdwritev(dst->fd, iop, ion) < n){
			fprint(2, "Failed to write to session %s: %r", dst->label);
			dst = &nilsess;
		}
		
		iop = io;
		ion = 1;
	}

	dtaskstate("");
}
