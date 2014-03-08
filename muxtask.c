#include "a.h"
#include "muxtask.h"
#include <mux.h>
#include <task.h>

#define INITIAL_FRAME_CAPACITY 128
#define HEADER_SIZE 8

// TODO: reuse frames
mux_frame_t*
muxreadframe(int fd)
{
	mux_frame_t* frame = mux_frame_create(INITIAL_FRAME_CAPACITY);
	int32_t off = 0;
	int32_t framed = -(HEADER_SIZE);
	while (framed < 0) {
		mux_frame_expand(frame, -framed);
		if (fdreadn(fd, &frame->data[off], -framed) != -(framed))
			return nil;
		frame->size += -framed;
		off += -framed;
		framed = mux_frame_iscomplete(frame);
	}
	return frame;
}
