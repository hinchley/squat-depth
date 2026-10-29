#include <math.h>
#include <stdio.h>

#include "depth.h"
#include "settings.h"
#include "track.h"
#include "types.h"

static u64 failures = 0;

// settings.h's calibration, which the tests run with.
static DepthCalibration calibration;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "depth_test: failed: %s\n", what);
  failures++;
}

// a keypoint at x and y, scoring well.
static Keypoint
At(f32 x, f32 y)
{
  return (Keypoint){x, y, 0.9f};
}

static void
TestMarginSign(void)
{
  // a horizontal thigh 100 pixels long, facing right, with the hip level, well above and
  // well below the knee.
  DepthGeometry level = DepthMeasure(At(100, 400), At(200, 400), 100, &calibration);
  f64 expected = depthKneeTopOffset - depthCreaseNormal;
  Expect(fabs(level.margin - expected) < 1e-6, "a level thigh's margin is the offsets'");
  Expect(
    DepthMeasure(At(100, 370), At(200, 400), 100, &calibration).margin < 0,
    "a high hip is above"
  );
  Expect(
    DepthMeasure(At(100, 430), At(200, 400), 100, &calibration).margin > 0,
    "a low hip is below"
  );
  Expect(level.kneeTopY < 400 && level.kneeTopX == 200, "the top of the knee is above it");

  // the same thigh facing left is judged the same.
  DepthGeometry left = DepthMeasure(At(200, 400), At(100, 400), 100, &calibration);
  Expect(fabs(left.margin - level.margin) < 1e-6, "facing left measures the same");
  Expect(left.kneeTopY < 400, "the top of the knee is above it facing left");
}

static void
TestMarginScale(void)
{
  // offsets scale with the thigh, so the margin is the same at any distance from the camera.
  f64 near = DepthMeasure(At(100, 420), At(300, 400), 200, &calibration).margin;
  f64 far = DepthMeasure(At(50, 210), At(150, 200), 100, &calibration).margin;
  Expect(fabs(near - far) < 1e-6, "the margin does not depend on scale");
}

static void
TestProbability(void)
{
  Expect(fabs(DepthProbability(0, 0.05) - 0.5) < 1e-12, "a zero margin is even odds");
  bool symmetric = true;
  bool rising = true;
  f64 previous = 0;
  for (i32 step = -20; step <= 20; step++) {
    f64 margin = 0.01 * step;
    f64 probability = DepthProbability(margin, 0.05);
    symmetric = symmetric && fabs(probability + DepthProbability(-margin, 0.05) - 1) < 1e-12;
    rising = rising && probability > previous;
    previous = probability;
  }
  Expect(symmetric, "the probability is symmetric about a zero margin");
  Expect(rising, "the probability rises with the margin");
  Expect(fabs(DepthProbability(0.05, 0.05) - 0.841'344'746) < 1e-6, "one deviation is 84%");
  Expect(DepthProbability(0.01, 0.1) < DepthProbability(0.01, 0.02), "doubt lowers it");
  Expect(DepthProbability(0.01, 0) == 1 && DepthProbability(-0.01, 0) == 0, "no doubt");
}

static void
TestThighAngle(void)
{
  Expect(fabs(DepthThighAngle(At(100, 400), At(200, 400))) < 1e-9, "a level thigh is 0°");
  Expect(fabs(DepthThighAngle(At(100, 300), At(200, 400)) - 45) < 1e-9, "hip above is +45°");
  Expect(fabs(DepthThighAngle(At(200, 300), At(100, 400)) - 45) < 1e-9, "facing left too");
  Expect(fabs(DepthThighAngle(At(100, 410), At(200, 400)) + 5.710'593) < 1e-5, "below");
}

static void
TestAssessFindsDeepestFrame(void)
{
  // a squat whose hip goes 20 pixels below the knee at frame 30, scored well, with the knee
  // 100 pixels in front of the hip.
  static constexpr u64 count = 60;
  Keypoint hips[count];
  Keypoint knees[count];
  bool found[count];
  f64 height[count];
  for (u64 index = 0; index < count; index++) {
    f64 below = 20 - fabs((f64)index - 30);
    hips[index] = (Keypoint){100, (f32)(400 + below), 1};
    knees[index] = (Keypoint){200, 400, 1};
    found[index] = true;
    height[index] = -below / 100;
  }
  Track track = {
    .count = count,
    .side = TrackSideRight,
    .thighLength = 100,
    .hips = hips,
    .knees = knees,
    .found = found,
    .height = height,
    .reps = {{.start = 5, .bottom = 20, .end = 55, .complete = true}},
    .repCount = 1
  };
  Depth depths[maxReps];
  DepthAssess(&track, &calibration, depths);
  Expect(depths[0].bottom == 30, "the bottom is the deepest frame");
  Expect(track.reps[0].bottom == 30, "the rep's bottom is moved to it");
  Expect(depths[0].passed && depths[0].probability > 0.9, "a deep squat passes");
  Expect(depths[0].thighAngle < 0, "the hip is below the knee");
  Expect(depths[0].scoreDeviation == 0, "perfect scores add no doubt");
  Expect(depths[0].jitterDeviation < 0.02, "a clean signal has little jitter");
  Expect(depths[0].deviation >= depthModelDeviation, "the model's doubt is always there");
}

int
main(void)
{
  calibration = DepthDefaultCalibration();
  TestMarginSign();
  TestMarginScale();
  TestProbability();
  TestThighAngle();
  TestAssessFindsDeepestFrame();
  return failures ? 1 : 0;
}
