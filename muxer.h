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
	
	Tdiscarded = 66,
	Tlease = -67,
	
	Rerr = -128,
	
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

typedef
struct Session
{
	char label[64];
	
	/* need a lock for the fd */
	int fd;
} Session;

Session* mksession(int fd, char *fmt, ...);
void readsession(Session *s, Channel *c);

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
	Muxhdr hd;
	Channel *donec;
	Session *s;
} Muxmesg;
