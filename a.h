#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <task.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "arg.h"

#define	nelem(x)	(sizeof(x)/sizeof((x)[0]))

/* From $PLAN9/src/cmd/fossil/pack.c */
#define	U8GET(p)	((p)[0])
#define	U16GET(p)	(((p)[0]<<8)|(p)[1])
#define	U24GET(p)	(((p)[0]<<16)|((p)[1]<<8)|(p)[2])
#define	U32GET(p)	(((p)[0]<<24)|((p)[1]<<16)|((p)[2]<<8)|(p)[3])
#define	U48GET(p)	(((uvlong)U16GET(p)<<32)|(uvlong)U32GET((p)+2))
#define	U64GET(p)	(((uvlong)U32GET(p)<<32)|(uvlong)U32GET((p)+4))

#define	U8PUT(p,v)	(p)[0]=(v)
#define	U16PUT(p,v)	(p)[0]=(v)>>8;(p)[1]=(v)
#define	U24PUT(p,v)	(p)[0]=((v)>>16)&0xFF;(p)[1]=((v)>>8)&0xFF;(p)[2]=(v)&0xFF
#define	U32PUT(p,v)	(p)[0]=((v)>>24)&0xFF;(p)[1]=((v)>>16)&0xFF;(p)[2]=((v)>>8)&0xFF;(p)[3]=(v)&0xFF
#define	U48PUT(p,v,t32)	t32=(v)>>32;U16PUT(p,t32);t32=(v);U32PUT((p)+2,t32)
#define	U64PUT(p,v,t32)	t32=(v)>>32;U32PUT(p,t32);t32=(v);U32PUT((p)+4,t32)


typedef unsigned int uint32;
typedef unsigned long long uint64;
typedef unsigned int uint;
typedef int int32;
typedef long long int64;
typedef unsigned char uchar;
typedef unsigned long uintptr;

#define nil (void*)0

void* emalloc(uint n);

int netmunge(char *s);
int fdreadn(int fd, void *buf, uint n);
