#include "a.h"
#include "muxer.h"

typedef struct	Route	Route;

struct Route
{
	Session *sess;
	Route *next;
};

static Route *routes = nil;
static int nroutes = 0;

void
routeadd(Session *sess)
{
	Route *r;
	
	r = emalloc(sizeof *r);
	r->sess = sess;
	r->next = routes;
	routes = r;
	nroutes++;
}

void
routedel(Session *sess)
{
	Route **rp, *r;
	
	rp = &routes;
	for(rp=&routes; *rp!=nil && (*rp)->sess!=sess; rp=&(*rp)->next)
		;

	if((r=*rp) == nil)
		return;
		
	*rp = r->next;
	free(r);
}

Session*
routelookup()
{
	int i;
	Route *r;
	
	if(routes == nil)
		return nil;

	for(i = rand() % nroutes, r = routes; i>0; i--)
		r = r->next;

	return r->sess;
}
