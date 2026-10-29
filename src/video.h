#pragma once

// decodes a video file into upright rgb frames, applying any rotation in its metadata.
#include "image.h"
#include "strbuf.h"
#include "types.h"

typedef struct Video Video;

enum VideoRead {
  VideoReadFrame,
  VideoReadEnd,
  VideoReadError
};

// a summary of the video, as it will be shown upright.
typedef struct VideoInfo {
  u32 width;
  u32 height;

  // the clockwise rotation applied to each decoded frame: 0, 90, 180 or 270.
  u32 rotation;

  // frames per second, or 0 when the file does not say.
  f64 frameRate;

  // seconds, or 0 when the file does not say.
  f64 duration;
} VideoInfo;

// the opened video, or nullptr with the reason appended to error.
Video *
VideoOpen(const char *path, StrBuf *error);

// the video's size, rotation, frame rate and duration.
VideoInfo
VideoGetInfo(const Video *video);

// decodes the next frame into frame, upright, and sets seconds to its time from the start.
// frame's storage is reused from call to call.
enum VideoRead
VideoNextFrame(Video *video, Image *frame, f64 *seconds, StrBuf *error);

// closes the file and frees the video.
void
VideoClose(Video *video);
