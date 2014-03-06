
#include <mux.h>

enum
{
  FRAME_SUCCESS = 0,
  FRAME_FAILURE_EOF = -1
};

/**
 * Either successfully completes the given mux_frame_t from the given fd, or returns a
 * negative error code indicating that the frame is incomplete, and fd is unusable.
 */
int muxreadframe(mux_frame_t* frame, int fd);
