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
	Rping = -63,
	
	Tdiscarded = -62,
	Tlease = -61,
	
	/* Could be either */
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

typedef
struct Session
{
	char label[64];
	int fd;
} Session;

Session* mksession(int fd, char *fmt, ...);
void readsession(Session *s, Channel *c);

typedef 
struct Muxmesg
{
	uint origtag;

	Session *s;
	Muxframe *f;
} Muxmesg;
