#include "a.h"
#include "muxer.h"

typedef struct	Route	Route;

struct Route
{
	Session *sess;
	Route *next;
};

static Route *routes = nil;
static Route *nextroute = nil;

void
routeadd(Session *sess)
{
	Route *r;
	
	r = emalloc(sizeof *r);
	r->sess = sess;
	r->next = routes;
	routes = r;
	nextroute = nil;
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
	nextroute = nil;
}

Session*
routelookup()
{
	Route *r;

	if(nextroute == nil)
		nextroute = routes;

	if((r = nextroute) == nil)
		return nil;

	nextroute = nextroute->next;
	return r->sess;
}
