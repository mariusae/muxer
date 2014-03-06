

typedef
struct Muxframe
{
	uint magic;

	uint n;
	uchar buf[];
} Muxframe;

typedef
struct Muxmsg
{
	uint magic;

	/* header: */
	uint type;
	uint32 tag;

	/* body: */
	uint n;
	uchar buf[];
} Muxmsg;

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

Muxframe* muxreadframe(int fd);
Muxmsg* muxF2M(Muxframe* f);
Muxframe* muxM2F(Muxmsg *m);
