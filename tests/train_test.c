#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "depth.h"
#include "settings.h"
#include "strbuf.h"
#include "track.h"
#include "train.h"
#include "types.h"

static u64 failures = 0;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "train_test: failed: %s\n", what);
  failures++;
}

// whether line parses into manifest.
static bool
Parses(TrainManifest *manifest, const char *line, const char *directory)
{
  StrBuf error = {};
  bool parsed = TrainParseLine(line, 1, directory, manifest, &error);
  StrBufFree(&error);
  return parsed;
}

static void
TestParse(void)
{
  TrainManifest manifest = {};
  Expect(Parses(&manifest, "", "videos"), "a blank line parses");
  Expect(Parses(&manifest, "  # a comment", "videos"), "a comment parses");
  Expect(!manifest.labelCount, "blank lines and comments add nothing");

  Expect(Parses(&manifest, "meet/lifter 4.mp4  2  fail  borderline", "videos"), "a label");
  Expect(Parses(&manifest, "/clips/a.mp4 1 PASS", "videos"), "a label without certainty");
  Expect(Parses(&manifest, "meet/lifter 4.mp4\t1\tpass\tclear\r", "videos"), "with tabs");
  Expect(manifest.labelCount == 3 && manifest.videoCount == 2, "videos are listed once");
  if (manifest.labelCount == 3) {
    const TrainLabel *first = &manifest.labels[0];
    Expect(
      strcmp(manifest.videos[first->video], "videos/meet/lifter 4.mp4") == 0,
      "a relative path with a space is taken from the manifest's directory"
    );
    Expect(
      first->squat == 2 && !first->passed && first->certainty == TrainCertaintyBorderline,
      "the squat, verdict and certainty are read"
    );
    Expect(strcmp(manifest.videos[manifest.labels[1].video], "/clips/a.mp4") == 0, "absolute");
    Expect(
      manifest.labels[1].passed && manifest.labels[1].certainty == TrainCertaintyUnstated,
      "certainty is optional"
    );
    Expect(manifest.labels[2].video == first->video, "the same video is found again");
  }

  Expect(!Parses(&manifest, "a.mp4 1 maybe", ""), "a verdict must be pass or fail");
  Expect(!Parses(&manifest, "a.mp4 0 pass", ""), "squats count from 1");
  Expect(!Parses(&manifest, "a.mp4 one pass", ""), "a squat is a number");
  Expect(!Parses(&manifest, "1 pass clear", ""), "a path is needed");
  Expect(manifest.labelCount == 3, "bad lines add nothing");
  TrainManifestFree(&manifest);
}

static constexpr u64 videoCount = 24;
static constexpr u64 frameCount = 60;

// a video's hip and knee: one squat whose hip joint ends some thigh lengths below the knee
// joint at frame 30, with a level thigh 100 pixels long.
typedef struct Video {
  Keypoint hips[frameCount];
  Keypoint knees[frameCount];
  bool found[frameCount];
  f64 height[frameCount];
} Video;

static Video videos[videoCount];

static void
TestFitFindsTheKnee(void)
{
  // with a level thigh, the margin is below + kneeTop - depthCreaseNormal. the squats are
  // labelled with the top of the knee truly 0.15 up, so they pass when the hip joint ends
  // more than 0.05 below the knee joint, and their depths straddle that.
  static constexpr f64 trueKneeTop = 0.15;
  TrainManifest manifest = {};
  Track tracks[videoCount];
  bool tracked[videoCount];
  for (u64 video = 0; video < videoCount; video++) {
    f64 below = -0.065 + 0.01 * (f64)video;
    Video *frames = &videos[video];
    for (u64 index = 0; index < frameCount; index++) {
      f64 depth = below * (1 - fabs((f64)index - 30) / 30);
      frames->hips[index] = (Keypoint){100, (f32)(400 + 100 * depth), 1};
      frames->knees[index] = (Keypoint){200, 400, 1};
      frames->found[index] = true;
      frames->height[index] = -depth;
    }
    tracks[video] = (Track){
      .count = frameCount,
      .side = TrackSideRight,
      .thighLength = 100,
      .hips = frames->hips,
      .knees = frames->knees,
      .found = frames->found,
      .height = frames->height,
      .reps = {{.start = 2, .bottom = 30, .end = 58, .complete = true}},
      .repCount = 1
    };
    tracked[video] = true;

    char line[64];
    bool passed = below + trueKneeTop - depthCreaseNormal > 0;
    snprintf(
      line,
      sizeof(line),
      "video-%02" PRIu64 ".mp4 1 %s",
      video,
      passed ? "pass" : "fail"
    );
    Expect(Parses(&manifest, line, ""), "the synthetic manifest parses");
  }

  TrainResult result;
  StrBuf report = {};
  bool fitted = TrainFit(&manifest, tracks, tracked, &result, &report);
  Expect(fitted && result.usable == videoCount, "every squat is judged");
  Expect(result.suggested, "enough squats give a suggestion");
  Expect(
    fabs(result.fitted.kneeTopOffset - trueKneeTop) <= 0.015,
    "the fit finds the true top of the knee"
  );
  Expect(
    result.kneeTopLow <= result.fitted.kneeTopOffset &&
      result.fitted.kneeTopOffset <= result.kneeTopHigh,
    "the likely range holds the fit"
  );
  Expect(
    strstr(StrBufText(&report), "static constexpr f64 depthKneeTopOffset = ") != nullptr,
    "the report gives the settings to apply"
  );
  StrBufFree(&report);

  // a squat number the program never found is skipped, and too few squats suggest nothing.
  TrainManifest few = {};
  Expect(Parses(&few, "video-00.mp4 1 fail", ""), "a short manifest parses");
  Expect(Parses(&few, "video-01.mp4 2 fail", ""), "a short manifest parses");
  StrBuf fewReport = {};
  fitted = TrainFit(&few, tracks, tracked, &result, &fewReport);
  Expect(fitted && result.usable == 1, "a missing squat is skipped");
  Expect(!result.suggested, "one squat suggests nothing");
  Expect(strstr(StrBufText(&fewReport), "skipped line") != nullptr, "the skip is reported");
  StrBufFree(&fewReport);

  tracked[0] = false;
  TrainManifest none = {};
  Expect(Parses(&none, "video-00.mp4 1 fail", ""), "a one-line manifest parses");
  StrBuf noneReport = {};
  Expect(!TrainFit(&none, tracks, tracked, &result, &noneReport), "nothing to judge fails");
  StrBufFree(&noneReport);

  TrainManifestFree(&manifest);
  TrainManifestFree(&few);
  TrainManifestFree(&none);
}

int
main(void)
{
  TestParse();
  TestFitFindsTheKnee();
  return failures ? 1 : 0;
}
