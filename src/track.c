#include "track.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

// orders f64s ascending, for qsort.
static int
CompareF64(const void *left, const void *right)
{
  f64 a = *(const f64 *)left;
  f64 b = *(const f64 *)right;
  return (a > b) - (a < b);
}

// the value at fraction of the way through count values, which are sorted in place.
static f64
Percentile(f64 *values, u64 count, f64 fraction)
{
  qsort(values, count, sizeof(f64), CompareF64);
  u64 index = (u64)(fraction * (f64)(count - 1) + 0.5);
  return values[index];
}

// whether the keypoint is confident enough to use.
static bool
Found(const Keypoint *keypoint)
{
  return keypoint->score >= poseMinScore;
}

// the side whose hip and knee have the higher mean score: the side facing the camera.
static enum TrackSide
ChooseSide(const Pose *poses, u64 count)
{
  f64 left = 0;
  f64 right = 0;
  for (u64 index = 0; index < count; index++) {
    const Keypoint *keypoints = poses[index].keypoints;
    left += keypoints[PoseLeftHip].score + keypoints[PoseLeftKnee].score;
    right += keypoints[PoseRightHip].score + keypoints[PoseRightKnee].score;
  }
  return right > left ? TrackSideRight : TrackSideLeft;
}

void
TrackSmooth(const f64 *raw, const bool *found, u64 count, f64 *smoothed)
{
  // fills gaps by joining the found values either side with a straight line, holding the
  // first and last found values out to the ends.
  f64 *filled = MemoryResize(nullptr, count, sizeof(f64));
  u64 previous = UINT64_MAX;
  for (u64 index = 0; index < count; index++) {
    if (!found[index]) continue;
    filled[index] = raw[index];
    u64 from = previous == UINT64_MAX ? 0 : previous + 1;
    for (u64 gap = from; gap < index; gap++) {
      f64 along = previous == UINT64_MAX ? 1 : (f64)(gap - previous) / (f64)(index - previous);
      f64 before = previous == UINT64_MAX ? raw[index] : raw[previous];
      filled[gap] = before + (raw[index] - before) * along;
    }
    previous = index;
  }
  for (u64 gap = previous + 1; previous != UINT64_MAX && gap < count; gap++) {
    filled[gap] = raw[previous];
  }

  // a median filter takes out single-frame glitches, then a moving average the jitter.
  f64 *median = MemoryResize(nullptr, count, sizeof(f64));
  for (u64 index = 0; index < count; index++) {
    f64 window[trackMedianWindow];
    u64 size = 0;
    u64 from = index >= trackMedianWindow / 2 ? index - trackMedianWindow / 2 : 0;
    for (u64 at = from; at < count && at <= index + trackMedianWindow / 2; at++) {
      window[size++] = filled[at];
    }
    qsort(window, size, sizeof(f64), CompareF64);
    median[index] = size % 2 ? window[size / 2]
                             : (window[size / 2 - 1] + window[size / 2]) / 2;
  }
  for (u64 index = 0; index < count; index++) {
    f64 total = 0;
    u64 size = 0;
    u64 from = index >= trackAverageWindow / 2 ? index - trackAverageWindow / 2 : 0;
    for (u64 at = from; at < count && at <= index + trackAverageWindow / 2; at++, size++) {
      total += median[at];
    }
    smoothed[index] = total / (f64)size;
  }
  free(filled);
  free(median);
}

u64
TrackFindReps(const f64 *height, u64 count, Rep *reps, u64 capacity)
{
  if (!count) return 0;
  f64 *sorted = MemoryResize(nullptr, count, sizeof(f64));
  memcpy(sorted, height, count * sizeof(f64));
  f64 standing = Percentile(sorted, count, trackStandingPercentile);
  free(sorted);

  // a squat starts when the height drops from standing to below the drop, and ends when it
  // comes back near standing. the start is the last frame standing before the drop, or the
  // first frame when the video starts mid-squat.
  u64 repCount = 0;
  u64 lastStanding = 0;
  bool squatting = false;
  Rep rep = {};
  for (u64 index = 0; index < count && repCount < capacity; index++) {
    f64 value = height[index];
    if (!squatting) {
      if (value >= standing - trackStandingTolerance) lastStanding = index;
      else if (value <= standing - trackMinDrop) {
        squatting = true;
        rep = (Rep){.start = lastStanding, .bottom = index};
      }
      continue;
    }

    if (value < height[rep.bottom]) rep.bottom = index;
    if (value >= standing - trackReturnTolerance) {
      rep.end = index;
      rep.complete = true;
      reps[repCount++] = rep;
      squatting = false;
      lastStanding = index;
    }
  }
  if (squatting && repCount < capacity) {
    rep.end = count - 1;
    reps[repCount++] = rep;
  }
  return repCount;
}

bool
TrackBuild(const Pose *poses, u64 count, Track *track, StrBuf *error)
{
  *track = (Track){.count = count, .side = ChooseSide(poses, count)};
  track->hips = MemoryResize(nullptr, count, sizeof(Keypoint));
  track->knees = MemoryResize(nullptr, count, sizeof(Keypoint));
  track->found = MemoryResize(nullptr, count, sizeof(bool));
  track->height = MemoryResize(nullptr, count, sizeof(f64));

  enum PoseKeypoint nearHip = track->side == TrackSideLeft ? PoseLeftHip : PoseRightHip;
  enum PoseKeypoint nearKnee = track->side == TrackSideLeft ? PoseLeftKnee : PoseRightKnee;
  enum PoseKeypoint farHip = track->side == TrackSideLeft ? PoseRightHip : PoseLeftHip;
  enum PoseKeypoint farKnee = track->side == TrackSideLeft ? PoseRightKnee : PoseLeftKnee;
  f64 *lengths = MemoryResize(nullptr, count, sizeof(f64));
  u64 foundCount = 0;
  for (u64 index = 0; index < count; index++) {
    const Keypoint *keypoints = poses[index].keypoints;
    bool near = Found(&keypoints[nearHip]) && Found(&keypoints[nearKnee]);
    bool far = Found(&keypoints[farHip]) && Found(&keypoints[farKnee]);
    track->hips[index] = keypoints[near || !far ? nearHip : farHip];
    track->knees[index] = keypoints[near || !far ? nearKnee : farKnee];
    track->found[index] = near || far;
    if (!track->found[index]) continue;
    const Keypoint *hip = &track->hips[index];
    const Keypoint *knee = &track->knees[index];
    lengths[foundCount++] = hypot(hip->x - knee->x, hip->y - knee->y);
  }
  if (foundCount < trackMinFrames) {
    StrBufAppendFormat(
      error,
      "no lifter found: a hip and knee were found in only %" PRIu64 " of %" PRIu64 " frames",
      foundCount,
      count
    );
    free(lengths);
    return false;
  }
  track->thighLength = Percentile(lengths, foundCount, 0.5);
  free(lengths);

  f64 *raw = MemoryResize(nullptr, count, sizeof(f64));
  for (u64 index = 0; index < count; index++) {
    raw[index] = (track->knees[index].y - track->hips[index].y) / track->thighLength;
  }
  TrackSmooth(raw, track->found, count, track->height);
  free(raw);
  track->repCount = TrackFindReps(track->height, count, track->reps, maxReps);
  return true;
}

void
TrackFree(Track *track)
{
  free(track->hips);
  free(track->knees);
  free(track->found);
  free(track->height);
  *track = (Track){};
}
