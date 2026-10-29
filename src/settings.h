#pragma once

// every tunable value, in one place.
#include "types.h"

// the name errors are prefixed with.
static constexpr char programName[] = "squat-depth";

// where the pose model is looked for when --model is not given.
static constexpr char defaultModelPath[] = "models/rtmpose-m.onnx";

// the pose model's input size in pixels, and the bins per pixel of its SimCC outputs.
static constexpr u32 poseInputWidth = 192;
static constexpr u32 poseInputHeight = 256;
static constexpr f32 poseSplitRatio = 2.0f;

// the ImageNet mean and standard deviation of red, green and blue that the model was
// trained to see subtracted and divided out.
static constexpr f32 poseMean[3] = {123.675f, 116.28f, 103.53f};
static constexpr f32 poseDeviation[3] = {58.395f, 57.12f, 57.375f};

// the threads the pose model runs on. more barely speeds it up, as the model is small.
static constexpr int poseThreads = 4;

// the factor a person's box is grown by before it is cropped, as the model was trained.
static constexpr f32 poseBoxPadding = 1.25f;

// the fraction of its size a box drawn round the keypoints is grown by on each side, to
// take in the head and feet beyond them.
static constexpr f32 poseKeypointMargin = 0.1f;

// the lowest score a keypoint counts as found with.
static constexpr f32 poseMinScore = 0.3f;

// the lowest mean score of the hips, knees and ankles at which the person is still being
// followed, rather than searched for again in the whole frame.
static constexpr f32 poseFollowMinScore = 0.4f;

// where --dump-frames writes its pngs.
static constexpr char dumpDirectory[] = "frames";

// the most squats one video is searched for.
static constexpr u64 maxReps = 5;

// rep detection follows the hip's height above the knee, in thigh lengths: about 1 standing
// and 0 or less at depth. the sample videos' walk-outs dip to about 0.85.

// the fewest frames with a found hip and knee a video needs to be judged.
static constexpr u64 trackMinFrames = 10;

// the windows, in frames, of the median filter and then the moving average that smooth the
// height.
static constexpr u64 trackMedianWindow = 5;
static constexpr u64 trackAverageWindow = 3;

// the percentile of the height taken as standing.
static constexpr f64 trackStandingPercentile = 0.9;

// how far below standing the height can be while still standing.
static constexpr f64 trackStandingTolerance = 0.15;

// how far below standing the height must drop for a squat.
static constexpr f64 trackMinDrop = 0.5;

// how close to standing the height must return to end a squat.
static constexpr f64 trackReturnTolerance = 0.25;

// depth is judged from surface landmarks the model does not find, so they are placed from
// the hip and knee joints it does: along the thigh from hip to knee, and along the thigh's
// upward normal, in thigh lengths. the values are averages over the bottom frames of the two
// sample videos, where the edges of the leg were found in brightness profiles to about a
// pixel, on a 79 pixel thigh.

// the top of the knee is 23 pixels (0.29) above the knee keypoint in one video and 12 to 16
// (0.15 to 0.20) in the other.
// searching the frame for the edge instead was tried and rejected: in one video a dark knee
// sleeve against a dark plate leaves no edge, and in the other a highlight lies 6 pixels
// below it.
static constexpr f64 depthKneeTopOffset = 0.24;

// the hip crease, where the top of the thigh meets the hip, is 14 pixels (0.18) above the
// hip keypoint in one video and 17 to 18 (0.22) in the other, and about 20 pixels in front
// of it, as the keypoint marks the side of the hip.
static constexpr f64 depthCreaseAlong = 0.25;
static constexpr f64 depthCreaseNormal = 0.20;

// the pose model's landmark error, as a standard deviation in thigh lengths. measured
// surface to surface, both sample squats bottom out level to within 2 pixels, while the
// keypoints put them 0.06 below and 0.06 above. two samples are few, so this is rounded up.
static constexpr f64 depthModelDeviation = 0.07;

// the extra standard deviation, in thigh lengths, of a hip or knee scoring only
// poseMinScore. it falls in a straight line to nothing at a score of 1.
static constexpr f64 depthScoreDeviation = 0.08;

// the frames either side of the bottom over which the jitter and the scores are measured.
static constexpr u64 depthWindow = 5;

// training (--train) fits depthKneeTopOffset and depthModelDeviation to squats with known
// verdicts. a crease offset that is too low looks the same to it as a knee offset that is
// too high, so only the knee's is fitted. it searches these ranges in these steps.
static constexpr f64 trainKneeTopLowest = 0.0;
static constexpr f64 trainKneeTopHighest = 0.6;
static constexpr f64 trainKneeTopStep = 0.005;
static constexpr f64 trainDeviationLowest = 0.01;
static constexpr f64 trainDeviationHighest = 0.3;
static constexpr f64 trainDeviationStep = 0.0025;

// what training believes before it sees any verdicts: the values measured on the sample
// videos, give or take these spreads (the model error's as a factor, e^0.5 ≈ 1.6). this keeps
// a few verdicts, or verdicts that a threshold splits perfectly, from swinging the settings.
static constexpr f64 trainPriorKneeTopOffset = 0.24;
static constexpr f64 trainPriorKneeTopSpread = 0.10;
static constexpr f64 trainPriorModelDeviation = 0.07;
static constexpr f64 trainPriorDeviationSpread = 0.5;

// how far below the best fit, in log-likelihood, a value can be and still count as likely.
// 2 is about a 95% range.
static constexpr f64 trainLikelyDrop = 2.0;

// the fewest usable labelled squats training suggests settings from.
static constexpr u64 trainMinSquats = 10;
