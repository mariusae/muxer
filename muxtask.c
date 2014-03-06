#include "a.h"
#include <mux.h>
#include "muxtask.h"
#include <task.h>

int
muxreadframe(mux_frame_t* frame, int fd)
{
  int32_t off = 0;
  int32_t framed = -(HEADER_SIZE);
  while (framed < 0) {
    if (fdreadn(fd, frame->data[off], -framed) != -framed)
      return FRAME_FAILURE_EOF;
    frame->size += (-framed);
    framed = mux_frame_iscomplete(frame);
	}
	return FRAME_SUCCESS;
}
