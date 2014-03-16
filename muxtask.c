#include "a.h"
#include "muxtask.h"
#include <mux.h>
#include <task.h>

#define INITIAL_FRAME_CAPACITY 2
#define HEADER_SIZE 8

// TODO: reuse frames
mux_frame_t*
muxreadframe(int fd)
{
	mux_frame_t* frame = mux_frame_create(INITIAL_FRAME_CAPACITY);

	buf_t* header = mux_frame_next_buf(frame, HEADER_SIZE);
	if (fdreadn(fd, header->data, header->size) != header->size) {
		goto ignoreframe;
	}

	buf_t* body = mux_frame_next_buf(frame, mux_frame_decodesize(frame));
	if (fdreadn(fd, body->data, body->size) != body->size) {
		goto ignoreframe;
	}

	return frame;
ignoreframe:
	mux_frame_destroy(frame);
	return nil;
}
