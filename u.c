#include "a.h"

void*
emalloc(uint n)
{
	void *p;

	if((p = malloc(n)) == nil)
		abort();

	return p;
}

int
netmunge(char *s)
{
	char *p;

	if ((p=index(s, ':')) == nil)
		return -1;
		
	*p = nil;
	
	return atoi(&p[1]);
}

int
fdreadn(int fd, void *buf, uint n)
{
	int m, q = n;

	while(n>0){
		if((m = fdread(fd, buf, n)) < 0)
			return m;
		else if(m == 0)
			return q-n;
		n -= m;
	}

	return q;
}