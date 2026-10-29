#pragma once

// the text and images that report a video's squats and their depths.
#include "depth.h"
#include "image.h"
#include "strbuf.h"
#include "track.h"
#include "types.h"

// the confidence in a verdict reached with probability p of a good lift, as a whole
// percentage from 50 to 99: never 100, since no verdict from video is certain.
u32
ReportConfidence(f64 probability);

// appends the report on the squats in the video at path to report: a line naming the video
// and the count, then a line for each squat. seconds holds each frame's time, and depths one
// entry for each of the track's reps. detailed adds a line of working under each squat.
void
ReportSquats(
  const char *path,
  const f64 *seconds,
  const Track *track,
  const Depth *depths,
  bool detailed,
  StrBuf *report
);

// draws the squat's depth on frame, its bottom frame: the thigh from hip to knee, a dashed
// line across the top of the knee, a marker at the hip crease, and a label with the thigh's
// angle, the verdict and the confidence, green for a pass and red for a fail.
void
ReportDrawBottom(Image *frame, const Track *track, const Depth *depth);
