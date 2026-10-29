#include "depth.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

#include "memory.h"
#include "settings.h"

static constexpr f64 pi = 3.14159265358979323846;

DepthCalibration
DepthDefaultCalibration(void)
{
  return (DepthCalibration){
    .kneeTopOffset = depthKneeTopOffset,
    .creaseAlong = depthCreaseAlong,
    .creaseNormal = depthCreaseNormal,
    .modelDeviation = depthModelDeviation
  };
}

DepthGeometry
DepthMeasure(Keypoint hip, Keypoint knee, f64 thighLength, const DepthCalibration *calibration)
{
  // u runs along the thigh from hip to knee, and n is the normal to it that points up the
  // frame, which is toward the top of the thigh at the bottom of a squat.
  f64 dx = knee.x - hip.x;
  f64 dy = knee.y - hip.y;
  f64 length = hypot(dx, dy);
  f64 ux = length > 0 ? dx / length : 1;
  f64 uy = length > 0 ? dy / length : 0;
  f64 nx = uy;
  f64 ny = -ux;
  if (ny > 0) {
    nx = -nx;
    ny = -ny;
  }

  f64 along = calibration->creaseAlong;
  f64 normal = calibration->creaseNormal;
  f64 kneeTop = calibration->kneeTopOffset;
  DepthGeometry geometry = {
    .creaseX = hip.x + (along * ux + normal * nx) * thighLength,
    .creaseY = hip.y + (along * uy + normal * ny) * thighLength,
    .kneeTopX = knee.x + kneeTop * nx * thighLength,
    .kneeTopY = knee.y + kneeTop * ny * thighLength
  };
  // y points down the frame, so a crease below the knee has the larger y.
  geometry.margin = (geometry.creaseY - geometry.kneeTopY) / thighLength;
  return geometry;
}

f64
DepthProbability(f64 margin, f64 deviation)
{
  if (deviation <= 0) return margin > 0 ? 1 : margin < 0 ? 0 : 0.5;
  // the normal distribution's cumulative probability at margin / deviation.
  return 0.5 * erfc(-margin / (deviation * sqrt(2.0)));
}

f64
DepthThighAngle(Keypoint hip, Keypoint knee)
{
  return atan2(knee.y - hip.y, fabs(knee.x - hip.x)) * 180 / pi;
}

// judges one rep with calibration, given every frame's raw and smoothed margins.
static Depth
AssessRep(
  const Track *track,
  const Rep *rep,
  const DepthCalibration *calibration,
  const f64 *raw,
  const f64 *smoothed
)
{
  // the bottom is the found frame with the largest smoothed margin.
  u64 bottom = rep->bottom;
  f64 best = -DBL_MAX;
  for (u64 index = rep->start; index <= rep->end; index++) {
    if (!track->found[index] || smoothed[index] <= best) continue;
    best = smoothed[index];
    bottom = index;
  }

  // the jitter is the spread of the raw margin about the smoothed one near the bottom, and
  // the score is the lower of the hip's and knee's, averaged there.
  u64 from = bottom >= depthWindow ? bottom - depthWindow : 0;
  u64 to = bottom + depthWindow < track->count ? bottom + depthWindow : track->count - 1;
  f64 squares = 0;
  f64 scores = 0;
  u64 count = 0;
  for (u64 index = from; index <= to; index++) {
    if (!track->found[index]) continue;
    f64 residual = raw[index] - smoothed[index];
    squares += residual * residual;
    scores += fmin(track->hips[index].score, track->knees[index].score);
    count++;
  }
  f64 score = count ? fmin(fmax(scores / (f64)count, poseMinScore), 1) : poseMinScore;

  Depth depth = {
    .bottom = bottom,
    .margin = smoothed[bottom],
    .modelDeviation = calibration->modelDeviation,
    .jitterDeviation = count ? sqrt(squares / (f64)count) : 0,
    .scoreDeviation = depthScoreDeviation * (1 - score) / (1 - poseMinScore),
    .thighAngle = DepthThighAngle(track->hips[bottom], track->knees[bottom]),
    .geometry =
      DepthMeasure(track->hips[bottom], track->knees[bottom], track->thighLength, calibration)
  };
  depth.deviation = sqrt(
    depth.modelDeviation * depth.modelDeviation +
    depth.jitterDeviation * depth.jitterDeviation + depth.scoreDeviation * depth.scoreDeviation
  );
  depth.probability = DepthProbability(depth.margin, depth.deviation);
  depth.passed = depth.margin > 0;
  return depth;
}

void
DepthAssess(Track *track, const DepthCalibration *calibration, Depth *depths)
{
  if (!track->repCount) return;
  f64 *raw = MemoryResize(nullptr, track->count, sizeof(f64));
  f64 *smoothed = MemoryResize(nullptr, track->count, sizeof(f64));
  for (u64 index = 0; index < track->count; index++) {
    const Keypoint *hip = &track->hips[index];
    const Keypoint *knee = &track->knees[index];
    raw[index] = DepthMeasure(*hip, *knee, track->thighLength, calibration).margin;
  }
  TrackSmooth(raw, track->found, track->count, smoothed);
  for (u64 index = 0; index < track->repCount; index++) {
    depths[index] = AssessRep(track, &track->reps[index], calibration, raw, smoothed);
    track->reps[index].bottom = depths[index].bottom;
  }
  free(raw);
  free(smoothed);
}
