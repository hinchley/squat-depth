#pragma once

// follows the lifter's hip and knee through a video and finds the squats in it.
#include "pose.h"
#include "settings.h"
#include "strbuf.h"
#include "types.h"

// a squat, as frame indices.
typedef struct Rep {
  // the last frame standing before the descent.
  u64 start;

  // the frame where the hip is lowest relative to the knee, which DepthAssess moves to the
  // frame where the hip crease is deepest below the top of the knee.
  u64 bottom;

  // the first frame standing again, or the last frame when the video ends first.
  u64 end;
  bool complete;
} Rep;

enum TrackSide {
  TrackSideLeft,
  TrackSideRight
};

// the hip and knee through a video, and the squats found in them.
// zero it with {} before TrackBuild, and free it with TrackFree.
typedef struct Track {
  u64 count;

  // the side facing the camera, whose hip and knee are used wherever they are found.
  enum TrackSide side;

  // the median distance from hip to knee, in pixels.
  f64 thighLength;

  // the hip and knee of each frame, from the side facing the camera, or the other side where
  // that is not found. found is false where neither side's are.
  Keypoint *hips;
  Keypoint *knees;
  bool *found;

  // the hip's height above the knee in each frame, in thigh lengths, smoothed and with gaps
  // filled.
  f64 *height;

  Rep reps[maxReps];
  u64 repCount;
} Track;

// follows the hip and knee through the count poses of a video and finds its squats.
bool
TrackBuild(const Pose *poses, u64 count, Track *track, StrBuf *error);

// frees the track's arrays and leaves it empty.
void
TrackFree(Track *track);

// fills smoothed with raw, with the values where found is false interpolated from their
// neighbours, then median filtered and averaged. at least one value must be found.
void
TrackSmooth(const f64 *raw, const bool *found, u64 count, f64 *smoothed);

// the count of squats in the smoothed height, at most capacity, which are put in reps.
u64
TrackFindReps(const f64 *height, u64 count, Rep *reps, u64 capacity);
