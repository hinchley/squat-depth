#pragma once

// judges whether each squat went to depth: the hip crease strictly below the top of the knee.
#include "pose.h"
#include "track.h"
#include "types.h"

// the offsets that place the hip crease and the top of the knee, in thigh lengths, and the
// pose model's error in the margin. settings.h sets them, and training tries others.
typedef struct DepthCalibration {
  f64 kneeTopOffset;
  f64 creaseAlong;
  f64 creaseNormal;
  f64 modelDeviation;
} DepthCalibration;

// where the hip crease and the top of the knee are estimated to be, in frame pixels, from
// one frame's hip and knee.
typedef struct DepthGeometry {
  f64 creaseX;
  f64 creaseY;
  f64 kneeTopX;
  f64 kneeTopY;

  // how far the crease is below the top of the knee, in thigh lengths: above 0 is to depth.
  f64 margin;
} DepthGeometry;

// the depth of one squat, at its deepest frame.
typedef struct Depth {
  // the frame where the smoothed margin is largest.
  u64 bottom;

  // the smoothed margin at the bottom, in thigh lengths.
  f64 margin;

  // the standard deviations of the margin's error, in thigh lengths: the pose model's
  // landmark error, the frame-to-frame jitter around the bottom, the extra error of low
  // keypoint scores, and all three combined.
  f64 modelDeviation;
  f64 jitterDeviation;
  f64 scoreDeviation;
  f64 deviation;

  // the probability that the crease was below the top of the knee.
  f64 probability;
  bool passed;

  // the thigh's angle to horizontal at the bottom, in degrees, negative with the hip below
  // the knee.
  f64 thighAngle;

  // the crease and top of the knee in the bottom frame, unsmoothed.
  DepthGeometry geometry;
} Depth;

// the calibration in settings.h.
DepthCalibration
DepthDefaultCalibration(void);

// the crease and top of the knee estimated from hip and knee, with calibration's offsets
// scaled by thighLength in pixels.
DepthGeometry
DepthMeasure(
  Keypoint hip,
  Keypoint knee,
  f64 thighLength,
  const DepthCalibration *calibration
);

// the probability that a margin measured with the given standard deviation is above 0.
f64
DepthProbability(f64 margin, f64 deviation);

// the angle of the thigh from hip to knee to horizontal, in degrees, negative with the hip
// below the knee.
f64
DepthThighAngle(Keypoint hip, Keypoint knee);

// judges each of the track's squats into depths, one per rep, with calibration, and moves
// each rep's bottom to its deepest frame.
void
DepthAssess(Track *track, const DepthCalibration *calibration, Depth *depths);
