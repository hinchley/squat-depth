#include <stdio.h>
#include <string.h>

#include "depth.h"
#include "image.h"
#include "report.h"
#include "strbuf.h"
#include "track.h"
#include "types.h"

static u64 failures = 0;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "report_test: failed: %s\n", what);
  failures++;
}

// whether text contains part.
static bool
Contains(const StrBuf *text, const char *part)
{
  return strstr(StrBufText(text), part) != nullptr;
}

static constexpr u64 frameCount = 300;

// each frame's time at 30 frames a second.
static f64 seconds[frameCount];

static void
TestConfidence(void)
{
  Expect(ReportConfidence(0.5) == 50, "even odds is 50%");
  Expect(ReportConfidence(0.91) == 91, "a pass is its probability");
  Expect(ReportConfidence(0.16) == 84, "a fail is one minus its probability");
  Expect(ReportConfidence(0.9999) == 99 && ReportConfidence(0) == 99, "never 100%");
}

static void
TestTwoSquats(void)
{
  Track track = {
    .count = frameCount,
    .side = TrackSideRight,
    .thighLength = 80,
    .reps =
      {{.start = 30, .bottom = 60, .end = 90, .complete = true},
        {.start = 200, .bottom = 250, .end = 299, .complete = false}},
    .repCount = 2
  };
  Depth depths[maxReps] = {
    {.margin = 0.06, .probability = 0.91, .passed = true, .thighAngle = -4.8},
    {.margin = -0.07, .probability = 0.16, .passed = false, .thighAngle = 6.04}
  };
  StrBuf report = {};
  ReportSquats("/videos/squat.mp4", seconds, &track, depths, false, &report);
  Expect(strncmp(StrBufText(&report), "squat.mp4: 2 squats\n", 20) == 0, "the heading");
  Expect(
    Contains(
      &report,
      "  squat 1  bottom 2.00 s  thigh -4.8°  margin +0.06  PASS  91% confident this was a "
      "good lift\n"
    ),
    "a pass"
  );
  Expect(
    Contains(
      &report,
      "  squat 2  bottom 8.33 s  thigh +6.0°  margin -0.07  FAIL  84% confident this was not "
      "to depth\n"
    ),
    "a fail"
  );
  Expect(Contains(&report, "ends before the lifter stands up"), "an unfinished squat");
  Expect(!Contains(&report, "frames"), "no details unless asked");

  StrBufClear(&report);
  ReportSquats("squat.mp4", seconds, &track, depths, true, &report);
  Expect(Contains(&report, "right side facing the camera"), "details name the side");
  Expect(Contains(&report, "bottom frame 250"), "details give the bottom frame");
  StrBufFree(&report);
}

static void
TestNoSquats(void)
{
  Track track = {.count = frameCount, .side = TrackSideLeft, .thighLength = 80};
  StrBuf report = {};
  ReportSquats("standing.mp4", seconds, &track, nullptr, false, &report);
  Expect(strncmp(StrBufText(&report), "standing.mp4: 0 squats\n", 23) == 0, "no squats");
  Expect(Contains(&report, "never dropped"), "no squats says why");
  StrBufFree(&report);
}

static void
TestDrawBottom(void)
{
  Keypoint hips[1] = {{100, 300, 0.9f}};
  Keypoint knees[1] = {{180, 300, 0.9f}};
  Track track = {.count = 1, .thighLength = 80, .hips = hips, .knees = knees};
  Depth depth = {.passed = true, .probability = 0.9, .thighAngle = -1};
  DepthCalibration calibration = DepthDefaultCalibration();
  depth.geometry = DepthMeasure(hips[0], knees[0], track.thighLength, &calibration);
  Image frame = {};
  ImageResize(&frame, 360, 640);
  for (u64 index = 0; index < 360ull * 640 * 3; index++) frame.pixels[index] = 128;
  ReportDrawBottom(&frame, &track, &depth);

  // the crease marker and the label are green for a pass, and the knee-top line is yellow.
  const u8 *crease = frame.pixels + (u64)depth.geometry.creaseY * ImageStride(&frame) +
    (u64)depth.geometry.creaseX * 3;
  Expect(crease[1] > 150 && crease[0] < 100, "the crease marker is green");
  bool labelled = false;
  for (u64 index = 0; index < 40ull * ImageStride(&frame); index += 3) {
    labelled = labelled || (frame.pixels[index + 1] > 150 && frame.pixels[index] < 100);
  }
  Expect(labelled, "the label is drawn in green at the top");
  bool dashed = false;
  u64 row = (u64)(depth.geometry.kneeTopY + 0.5) * ImageStride(&frame);
  for (u64 x = 60; x < 220; x++) {
    const u8 *pixel = frame.pixels + row + x * 3;
    dashed = dashed || (pixel[0] > 200 && pixel[1] > 150 && pixel[2] < 100);
  }
  Expect(dashed, "the top of the knee is marked");
  ImageFree(&frame);
}

int
main(void)
{
  for (u64 index = 0; index < frameCount; index++) seconds[index] = (f64)index / 30;
  TestConfidence();
  TestTwoSquats();
  TestNoSquats();
  TestDrawBottom();
  return failures ? 1 : 0;
}
