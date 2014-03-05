#include "a.h"
#include "mux.h"
#include <task.h>


Muxframe*
muxreadframe(int fd)
{
	Muxframe *f;
	int n;
	char siz[4];

	if(fdreadn(fd, siz, 4) != 4)
		return nil;

	n = U32GET(siz);
	f = emalloc(sizeof(Muxframe)+n);
	f->n = n;

	if(fdreadn(fd, f->buf, n) != n){
		free(f);
		return nil;
	}


	return f;
}

Muxmsg*
muxF2M(Muxframe* f)
{
	uchar *p = f->buf;
	int n = f->n-4;
	Muxmsg *m = emalloc(sizeof(Muxmsg)+n);

	if(n<0)
		goto err;

	m->n = n;


	/* Header: type tag */
	m->type = U8GET(f->buf);
	m->tag = U24GET(f->buf+1);
	memcpy(m->buf, f->buf+4, m->n);

	return m;

  err:
	free(m);
	return nil;
}

Muxframe*
muxM2F(Muxmsg *m)
{
	Muxframe *f = emalloc(sizeof(Muxframe*)+m->n+4);

	f->n = m->n+4;

	U8PUT(f->buf, m->type);
	U24PUT(f->buf+1, m->tag);

	memcpy(f->buf+4, m->buf, m->n);
	return f;
}
