#include "a.h"
#include "muxer.h"

char
muxtype(Muxframe *f)
{
	if(f->n < 1)
		return 0;
	
	return U8GET(f->buf);
}

int32
muxtag(Muxframe *f)
{
	if(f->n < 4)
		return -1;
	
	return U24GET(f->buf+1);
}

int
muxsettype(Muxframe *f, char type)
{
	if(f->n < 1)
		return -1;
	
	U8PUT(f->buf, type);
	
	return 0;
}

int
muxsettag(Muxframe *f, uint32 tag)
{
	if(f->n < 4)
		return -1;
	
	U24PUT(f->buf+1, tag);

	return 0;
}
