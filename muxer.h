
#include "mux.h"

extern char* argv0;
extern int debug;

enum
{
	STACK = 32768,
	READAHEAD = 16 * 1024,
};


typedef struct Tags Tags;

Tags* mktags(int n);
void freetags(Tags *t);
int nexttag(Tags *t, void *p);
void* puttag(Tags *t, uint tag);

typedef
struct Session
{
	char label[128];
	// output for messages read by this session
	Channel* read_messages;
	// input for messages written to this session
	Channel* messages_to_write;
	// output to request an fdwait on this fd
	Channel* request_fdwait;
	// input for fdwait having returned for this fd
	Channel* fd_writable;
	int ok;
	int fd;
} Session;

extern Session *nilsess;

void sessinit();
Session* sesscreate(int fd, Channel *c, char *fmt, ...);
void sessfatal(Session *s, char *fmt, ...);

typedef 
struct Muxmesg
{
	mux_msg_t* msg;
	QLock *locked;
	Session **sp;
} Muxmesg;

typedef 
struct Tmesg
{
	tag_t tag;
	Session **sp;
} Tmesg;

typedef
struct Stats
{
	uint64 nreq;
	uint64 nsess;
} Stats;
extern Stats stats;

void servestatus(int fd);
