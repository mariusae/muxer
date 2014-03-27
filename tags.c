#include "a.h"
#include "muxer.h"

enum
{
	W = sizeof(uint)*8,
	Nhash = 1024
};

typedef struct El El;
struct El
{
	uint tag;
	void *p;
	El *next;
};

struct Tags
{
	uint maxtag;
	uint nw;

	El *tab[Nhash];
	
	uint bits[];
};


Tags*
mktags(int maxtag)
{
	Tags *t;
	uint siz;

	siz = sizeof(Tags)+sizeof(uint)*maxtag/W+1;

	t = emalloc(siz);
	memset(t, 0, siz);
	t->nw = maxtag/W+1;
	t->maxtag = maxtag;

	/* Reserve tag 1 */
	t->bits[0] = 1<<(sizeof(uint)*8-1);

	return t;
}

void
freetags(Tags *t)
{
	int i;
	El *el, *next;

	for(i=0; i<Nhash; i++){
		el = t->tab[i];
		while(el != nil){
			next = el->next;
			free(el);
			el = next;
		}
	}

	free(t);
}

int
nexttag(Tags *t, void *p)
{
	int w, b, hash;
	uint m;
	El *el;

	for(w=0; w<t->nw && ~t->bits[w] == 0; w++);
	if(w == t->nw)
		return -1;

	for(b=0; b<W; b++){
		m = 1<<(W-b-1);
		if((t->bits[w]&m) == 0)
			break;
	}
	assert(b < W);

	t->bits[w] |= m;

	el = emalloc(sizeof *el);
	el->tag = w*W+b;
	el->p = p;
	hash = el->tag%Nhash;
	el->next = t->tab[hash];
	t->tab[hash] = el;

	return el->tag;
}

void*
puttag(Tags *t, uint tag)
{
	int w, b, hash;
	El **elp, *el;
	void *p;

	w = tag/W;
	b = tag%W;
	t->bits[w] &= ~(1<<(W-b-1));

	hash = tag%Nhash;
	
	elp = &t->tab[hash];
	while(*elp != nil && (*elp)->tag != tag)
		elp = &(*elp)->next;
	if((el=*elp) == nil)
		return nil;
	p = el->p;
	*elp = el->next;
	free(el);

	return p;
}

void*
putnexttag(Tags *t)
{
	/* This could certainly be done more efficiently. */
	El *el;
	void *p;
	int i;

	for(i=0; i<Nhash; i++)
	if((el=t->tab[i]) != nil){
		t->tab[i] = el->next;
		p = el->p;
		free(el);
		return p;
	}

	return nil;
}
