#pragma once

// fits the depth calibration to squats with known verdicts, listed in a manifest file.
#include "depth.h"
#include "strbuf.h"
#include "track.h"
#include "types.h"

enum TrainCertainty {
  TrainCertaintyUnstated,
  TrainCertaintyClear,
  TrainCertaintyBorderline
};

// one squat's known verdict, from one line of a manifest.
typedef struct TrainLabel {
  // the index of the squat's video in the manifest's videos.
  u64 video;

  // the squat's number in the video, from 1, as the report numbers them.
  u64 squat;
  bool passed;
  enum TrainCertainty certainty;

  // the manifest line it came from, from 1.
  u64 line;
} TrainLabel;

// the labelled squats of a manifest, and the videos they are in.
// zero it with {} before use, and free it with TrainManifestFree.
typedef struct TrainManifest {
  TrainLabel *labels;
  u64 labelCount;
  u64 labelCapacity;

  // each video's path, once, with ~ expanded and relative paths made relative to the
  // manifest's directory.
  char **videos;
  u64 videoCount;
  u64 videoCapacity;
} TrainManifest;

// what a fit found.
typedef struct TrainResult {
  // the labelled squats that could be judged.
  u64 usable;

  // the best fitting calibration, and the likely range of each fitted value.
  DepthCalibration fitted;
  f64 kneeTopLow;
  f64 kneeTopHigh;
  f64 deviationLow;
  f64 deviationHigh;

  // whether there were enough usable squats to suggest the fitted calibration.
  bool suggested;
} TrainResult;

// adds the label on one manifest line, numbered number, to manifest. a line is
// "PATH SQUAT pass|fail [clear|borderline]", and a blank line or one starting with # is
// skipped. relative paths are taken from directory. false with a reason appended to error
// when the line is malformed.
bool
TrainParseLine(
  const char *line,
  u64 number,
  const char *directory,
  TrainManifest *manifest,
  StrBuf *error
);

// reads the manifest at path into manifest.
bool
TrainReadManifest(const char *path, TrainManifest *manifest, StrBuf *error);

// frees the manifest's labels and paths and leaves it empty.
void
TrainManifestFree(TrainManifest *manifest);

// fits the calibration to manifest's labels, judging each label's squat in tracks, one per
// video, where tracked is true. it judges with each candidate calibration, so it moves the
// reps' bottoms. appends the report to report and fills result. false when no labelled
// squat could be judged.
bool
TrainFit(
  const TrainManifest *manifest,
  Track *tracks,
  const bool *tracked,
  TrainResult *result,
  StrBuf *report
);
