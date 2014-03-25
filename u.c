#include "a.h"
#include <sys/uio.h>


void*
emalloc(uint n)
{
	void *p;

	if((p = calloc(n, 1)) == nil)
		abort();

	return p;
}

int
netmunge(char **s)
{
	char *p;

	if ((p=index(*s, ':')) == nil)
		return -1;

	if(p==*s)
		*s = "*";
	else
		*p = 0;

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

int
fdwritev(int fd, struct iovec *_iov, int iovn)
{
	int n, m, q, i, tot;
	struct iovec *iov;
	
	iov = alloca(sizeof(struct iovec)*iovn);
	memcpy(iov, _iov, sizeof(struct iovec)*iovn);

	for(i=0, n=0; i<iovn; i++)
		n += iov[i].iov_len;

	for(tot=0; tot<n; tot+=m){
		while((m = writev(fd, iov, iovn)) < 0 && errno == EAGAIN)
			fdwait(fd, 'w');
		if(m < 0)
			return m;
		if(m==0)
			break;

		/* Skip over completed buffers */
		for(q=0; (q+=iov->iov_len)<m; iov++, iovn--)
			;

		iov->iov_len -= q-m;
		iov->iov_base += q-m;
	}
	
	return tot;
}

int
fdprint(int fd, char *fmt, ...)
{
	char buf[256];
	va_list arg;
	
	va_start(arg, fmt);
	vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);
	
	return fdwrite(fd, buf, strlen(buf));
}

void
_dtaskstate(char *fmt, ...)
{
	va_list arg;

	fprint(2, "task %s: ", taskgetname());
	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
	fprint(2, "\n");
}