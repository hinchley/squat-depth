#include <math.h>
#include <stdio.h>
#include <string.h>

#include "strbuf.h"
#include "track.h"
#include "types.h"

static u64 failures = 0;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "track_test: failed: %s\n", what);
  failures++;
}

static constexpr u64 frameCapacity = 2'000;
static constexpr f64 pi = 3.14159265358979323846;

// a hip height signal built up a stretch at a time, at about 30 frames a second.
typedef struct Signal {
  f64 values[frameCapacity];
  bool found[frameCapacity];
  u64 count;
} Signal;

// adds frames frames standing at height.
static void
Hold(Signal *signal, f64 height, u64 frames)
{
  for (u64 index = 0; index < frames; index++) {
    signal->found[signal->count] = true;
    signal->values[signal->count++] = height;
  }
}

// adds a squat from standing (1) down to bottom and back, over frames frames.
static void
Squat(Signal *signal, f64 bottom, u64 frames)
{
  for (u64 index = 0; index < frames; index++) {
    f64 phase = (1 - cos(2 * pi * (f64)index / (f64)frames)) / 2;
    signal->found[signal->count] = true;
    signal->values[signal->count++] = 1 - (1 - bottom) * phase;
  }
}

// a pseudo-random number from -1 to 1, the same sequence every run.
static f64
Noise(u64 *state)
{
  *state = *state * 6'364'136'223'846'793'005ull + 1'442'695'040'888'963'407ull;
  return (f64)(*state >> 11) / (f64)(1ull << 53) * 2 - 1;
}

static void
TestStandingOnly(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 300);
  Rep reps[maxReps];
  Expect(
    TrackFindReps(signal.values, signal.count, reps, maxReps) == 0,
    "standing has no reps"
  );
}

static void
TestOneRepAfterDelay(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 150);
  Squat(&signal, 0, 90);
  Hold(&signal, 1, 120);
  Rep reps[maxReps];
  u64 count = TrackFindReps(signal.values, signal.count, reps, maxReps);
  Expect(count == 1, "one rep after a delay");
  if (count != 1) return;
  Expect(reps[0].complete, "the rep is complete");
  Expect(reps[0].bottom == 150 + 45, "the bottom is the lowest frame");
  Expect(reps[0].start >= 150 && reps[0].start < 195, "the start is before the bottom");
  Expect(reps[0].end > 195 && reps[0].end <= 240, "the end is after the bottom");
}

static void
TestThreeReps(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 60);
  for (u64 index = 0; index < 3; index++) {
    Squat(&signal, -0.1 + 0.1 * (f64)index, 80);
    Hold(&signal, 1, 20);
  }
  Rep reps[maxReps];
  u64 count = TrackFindReps(signal.values, signal.count, reps, maxReps);
  Expect(count == 3, "three reps");
  if (count != 3) return;
  Expect(reps[0].end <= reps[1].start && reps[1].end <= reps[2].start, "reps do not overlap");
  Expect(reps[1].bottom == 60 + 100 + 40, "the second rep's bottom");
}

static void
TestWalkOutIsNotARep(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 100);
  Squat(&signal, 0.8, 40);
  Hold(&signal, 1, 100);
  Squat(&signal, 0.6, 40);
  Hold(&signal, 1, 100);
  Rep reps[maxReps];
  Expect(TrackFindReps(signal.values, signal.count, reps, maxReps) == 0, "dips are not reps");
}

static void
TestUnfinishedRep(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 100);
  Squat(&signal, 0, 90);
  signal.count -= 30;
  Rep reps[maxReps];
  u64 count = TrackFindReps(signal.values, signal.count, reps, maxReps);
  Expect(count == 1, "an unfinished rep is still found");
  if (count != 1) return;
  Expect(!reps[0].complete, "an unfinished rep is marked so");
  Expect(reps[0].end == signal.count - 1, "an unfinished rep ends at the last frame");
}

static void
TestStartsMidSquat(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 200);
  u64 skip = 40;
  Squat(&signal, 0, 90);
  Hold(&signal, 1, 100);
  memmove(
    signal.values,
    signal.values + 200 + skip,
    (signal.count - 200 - skip) * sizeof(f64)
  );
  signal.count -= 200 + skip;
  Rep reps[maxReps];
  u64 count = TrackFindReps(signal.values, signal.count, reps, maxReps);
  Expect(count == 1, "a rep under way at the start is found");
  if (count == 1) Expect(reps[0].start == 0, "a rep under way starts at the first frame");
}

static void
TestCapacity(void)
{
  static Signal signal;
  signal = (Signal){};
  for (u64 index = 0; index < 8; index++) {
    Hold(&signal, 1, 20);
    Squat(&signal, 0, 60);
  }
  Hold(&signal, 1, 20);
  Rep reps[3];
  Expect(TrackFindReps(signal.values, signal.count, reps, 3) == 3, "reps stop at capacity");
}

static void
TestSmoothFillsGaps(void)
{
  f64 raw[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  bool found[10] = {false, true, true, false, false, false, true, true, true, false};
  f64 smoothed[10];
  TrackSmooth(raw, found, 10, smoothed);
  // a straight line survives filling, the median and the average, except within reach of
  // the ends, which hold the nearest found value.
  bool linear = true;
  for (u64 index = 3; index < 7; index++) {
    linear = linear && fabs(smoothed[index] - (f64)index) < 1e-9;
  }
  Expect(linear, "gaps in a straight line are filled along it");
}

static void
TestSmoothRemovesGlitch(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 50);
  signal.values[25] = -3;
  f64 smoothed[50];
  TrackSmooth(signal.values, signal.found, 50, smoothed);
  Expect(fabs(smoothed[25] - 1) < 1e-9, "a one-frame glitch is removed");
}

static void
TestNoisySignalWithGaps(void)
{
  static Signal signal;
  signal = (Signal){};
  Hold(&signal, 1, 120);
  Squat(&signal, 0.05, 100);
  Hold(&signal, 1, 60);
  Squat(&signal, 0.1, 100);
  Hold(&signal, 1, 120);
  u64 state = 7;
  for (u64 index = 0; index < signal.count; index++) {
    signal.values[index] += 0.04 * Noise(&state);
    if (Noise(&state) > 0.8) signal.found[index] = false;
  }
  static f64 smoothed[frameCapacity];
  TrackSmooth(signal.values, signal.found, signal.count, smoothed);
  Rep reps[maxReps];
  u64 count = TrackFindReps(smoothed, signal.count, reps, maxReps);
  Expect(count == 2, "noise and gaps do not change the count");
  if (count == 2) {
    Expect(fabs((f64)reps[0].bottom - 170) <= 8, "noise barely moves the bottom");
  }
}

// a pose with the given side's hip and knee at x, y and score, and the other side's at
// otherScore.
static Pose
LegPose(enum TrackSide side, f32 hipY, f32 kneeY, f32 score, f32 otherScore)
{
  Pose pose = {};
  bool left = side == TrackSideLeft;
  pose.keypoints[left ? PoseLeftHip : PoseRightHip] = (Keypoint){100, hipY, score};
  pose.keypoints[left ? PoseLeftKnee : PoseRightKnee] = (Keypoint){150, kneeY, score};
  pose.keypoints[left ? PoseRightHip : PoseLeftHip] = (Keypoint){100, hipY, otherScore};
  pose.keypoints[left ? PoseRightKnee : PoseLeftKnee] = (Keypoint){150, kneeY, otherScore};
  return pose;
}

static void
TestBuildChoosesSide(void)
{
  static Pose poses[400];
  for (u64 index = 0; index < 400; index++) {
    f64 phase = index >= 100 && index < 200 ? (1 - cos(2 * pi * (f64)(index - 100) / 100)) / 2
                                            : 0;
    // the thigh is 50 pixels across at the bottom, and 80 down when standing.
    f32 kneeY = 400;
    f32 hipY = (f32)(kneeY - 80 * (1 - phase));
    poses[index] = LegPose(TrackSideLeft, hipY, kneeY, 0.9f, 0.5f);
  }
  Track track = {};
  StrBuf error = {};
  bool built = TrackBuild(poses, 400, &track, &error);
  Expect(built, "a track builds from good poses");
  if (built) {
    Expect(track.side == TrackSideLeft, "the higher scoring side is chosen");
    Expect(track.repCount == 1, "the track finds the rep");
    Expect(track.found[0] && track.hips[0].score == 0.9f, "the chosen side's hip is used");
  }
  TrackFree(&track);
  StrBufFree(&error);
}

static void
TestBuildNeedsLegs(void)
{
  static Pose poses[50];
  for (u64 index = 0; index < 50; index++) {
    poses[index] = LegPose(TrackSideRight, 300, 380, index < 5 ? 0.9f : 0.1f, 0.1f);
  }
  Track track = {};
  StrBuf error = {};
  Expect(!TrackBuild(poses, 50, &track, &error), "too few legs found fails");
  Expect(error.length > 0, "too few legs found gives a reason");
  TrackFree(&track);
  StrBufFree(&error);
}

int
main(void)
{
  TestStandingOnly();
  TestOneRepAfterDelay();
  TestThreeReps();
  TestWalkOutIsNotARep();
  TestUnfinishedRep();
  TestStartsMidSquat();
  TestCapacity();
  TestSmoothFillsGaps();
  TestSmoothRemovesGlitch();
  TestNoisySignalWithGaps();
  TestBuildChoosesSide();
  TestBuildNeedsLegs();
  return failures ? 1 : 0;
}
