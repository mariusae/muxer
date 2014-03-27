extern char* argv0;
extern int debug;

enum
{
	STACK = 32768
};


typedef
struct Muxframe
{
	uint n;
	uchar buf[];
} Muxframe;

enum
{
	/* Application messages */
	Treq = 1,
	Rreq = -1,

	Tdispatch = 2,
	Rdispatch = -2,

	/* Control messages */
	Tdrain = 64,
	Rdrain = -64,
	Tping = 65,
	Rping = -65,
	
/*	Tdiscarded = 66,*/
/*	Tlease = -67,*/
	
/*	Rerr = -128,*/
	Tdiscarded = -62,
	Tlease = -61,

	Rerr = 127,
	
	Unknown = 0
};

char muxtype(Muxframe *f);
int32 muxtag(Muxframe *f);
int muxsettype(Muxframe *f, char type);
int muxsettag(Muxframe *f, uint32 tag);

typedef struct Tags Tags;

Tags* mktags(int n);
void freetags(Tags *t);
int nexttag(Tags *t, void *p);
void* puttag(Tags *t, uint tag);
void* putnexttag(Tags *t);

enum
{
	Sessfd,
	Sessdial
};

typedef
struct Session
{
	char label[128];
	int ok;
	int fd;
	int type;

	int dialnet;
	char *dialhost;
	int dialport;

	int npending;
	
	Tags *tags;
	
	Channel *callc;
	Channel *readc;
} Session;

Session* sesscreate(int fd, char *fmt, ...);
Session* sessdial(int net, char *host, int port, char *fmt, ...);
void sessfatal(Session *s, char *fmt, ...);

typedef
struct Muxhdr
{
	uint32 siz;
	char type;
	uint24 tag;
} Muxhdr;

typedef 
struct Muxmesg
{
	Channel *replyc;
	int savetag;
	Muxhdr hd;
	uchar body[];
} Muxmesg;

typedef
struct Stats
{
	uint64 nreq;
	uint64 nactivesess;
	uint64 nlifetimesess;
	uint64 nredials;
} Stats;
extern Stats stats;

void servestatus(int fd);

void routeadd(Session *sess);
Session *routelookup();
void routedel(Session *sess);

void copyframe(Session *dst, Session *src, Muxhdr *hd);