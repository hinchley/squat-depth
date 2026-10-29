# Architecture

How squat-depth is put together. `ReadMe.md` covers using it, and `STYLE.md` covers how the code is written.

## Data flow

```
video file ─► decode + rotate ─► pose per frame ─► rep detection ─► depth + confidence ─► report
 (video.c)       (video.c)          (pose.c)          (track.c)          (depth.c)        (report.c)
                                                                              └─► bottom-frame png
                                                                       (video.c again, report.c, image.c, font.c)
```

`main.c` gets the video's path, `video.c` decodes it into upright rgb frames, `pose.c` finds the lifter's pose in each, `track.c` finds the squats, `depth.c` judges each one's depth, and `report.c` writes what was found. With `--images`, `main.c` decodes the video a second time, up to the last squat's bottom, and `report.c` draws on each bottom frame. Frames aren't kept from the first pass, since a 30 s video is about 600 MB of rgb.

With `--train MANIFEST`, `main.c` instead runs the pipeline up to `track.c` on every video in a manifest, and `train.c` judges the labelled squats with many candidate calibrations. `depth.c` takes its offsets and model error as a `DepthCalibration` for this; everywhere else that is `DepthDefaultCalibration()`, the values in `settings.h`.

## Modules

| File | Purpose |
|---|---|
| `src/types.h` | `i8`–`u64`, `f32` and `f64`, which the code uses in place of the C integer and floating types |
| `src/settings.h` | every tunable value, as `static constexpr` |
| `src/memory.c/h` | `MemoryResize`, the only allocator, and `Fatal`, the exit for unrecoverable errors |
| `src/strbuf.c/h` | `StrBuf`, a growable string for error messages and report text, and `StrBufReadLine` |
| `src/media.c/h` | ffmpeg helpers shared by the modules that call it: error text, quiet logging |
| `src/video.c/h` | `Video`: opens a file, decodes frames, converts them to rgb, and turns them upright using the stream's display matrix |
| `src/image.c/h` | `Image`, an 8-bit rgb buffer, simple drawing (discs, solid and dashed lines, outlined and filled rectangles), and `ImageWritePng`, which encodes with ffmpeg's png encoder |
| `src/font.c/h` | a built-in 5×7 bitmap font, digits, letters drawn as capitals, and `+ - . % °`, for the images' labels |
| `src/pose.c/h` | `PoseModel`: runs RTMPose through the onnx runtime C API to find the 17 COCO keypoints, and follows the lifter from frame to frame |
| `src/track.c/h` | `Track`: picks the side facing the camera, follows its hip and knee, and finds the squats in the hip's height |
| `src/depth.c/h` | places the hip crease and the top of the knee from the hip and knee, and turns the gap between them into a verdict and a probability |
| `src/report.c/h` | the report: a line per squat with its verdict and confidence, optionally the working behind it, and the drawing on each bottom frame |
| `src/train.c/h` | `--train`: reads a manifest of squats with known verdicts and fits `depthKneeTopOffset` and `depthModelDeviation` to them (see `TRAIN.md`) |
| `src/path.c/h` | `~` expansion and file names |
| `src/main.c` | gets the video's path from `argv[1]`, or by prompting for it, and runs the pipeline |

## Video decoding

Phone videos are often stored sideways, with a display matrix that says how to turn them. `VideoOpen` reads the matrix from the stream's coded side data, rounds it to a clockwise turn of 0, 90, 180 or 270 degrees, and `VideoNextFrame` applies it after converting each frame to rgb with swscale. The result matches ffmpeg's own `-autorotate`. Flips in the matrix are ignored. A frame's time comes from its timestamp, not its index, because phone videos often have a variable frame rate.

## Pose estimation

RTMPose is a top-down model: it finds the keypoints of one person in a box around them. Its preprocessing and decoding are copied from the model's own `pipeline.json`:

- the box is grown by 1.25 and widened or heightened to 3:4 about its centre;
- the box is resampled bilinearly into a 192×256 crop, black beyond the frame's edges;
- the crop is normalised with the ImageNet mean and standard deviation, in rgb order;
- the model returns two SimCC outputs per keypoint, 384 bins across and 512 down, two bins per crop pixel. The keypoint is at the largest bin of each, and its score is the smaller of the two maxima.

There is no person detector. `PoseFollow` looks in the box around the previous frame's keypoints. At the start, or when the mean score of the hips, knees and ankles drops below `poseFollowMinScore`, it looks at the whole frame instead, then looks again at the box around what it found there.

The model runs on `poseThreads` (4) threads with spinning turned off. On the sample videos that is as fast as using every core (about 15 ms a frame), at a fraction of the CPU.

`scripts/fetch-model.sh` downloads the model and checks its sha256. `models/` is not committed.

## Finding squats

`TrackBuild` uses the hip and knee of the side with the higher mean score over the video, which is the side facing the camera. In a frame where that side's hip or knee falls below `poseMinScore`, it uses the other side's.

From those it makes one signal: the hip's height above the knee, divided by the median hip-to-knee distance. That is about 1 when standing, 0 with the hip joint level with the knee joint, and negative below. It doesn't depend on how far away the camera is. Frames with neither side found are filled by joining their neighbours with a straight line. The signal is then smoothed with a 5-frame median filter, which removes single-frame glitches, and a 3-frame moving average.

`TrackFindReps` takes the 90th percentile of the signal as standing. A squat begins when the signal drops `trackMinDrop` (0.5) below standing, and ends when it comes back within `trackReturnTolerance` (0.25). Its start is the last frame within `trackStandingTolerance` (0.15) of standing before that. This ignores the lead-in before the first squat, and the walk-out and re-rack, which in the samples dip to about 0.85. A squat still under way when the video ends is reported as not finished.

## Judging depth

The rule is about surfaces: the top of the thigh at the hip crease must be strictly below the top of the knee. The model finds joint centres, so `DepthMeasure` places the two surface points from them, in thigh lengths `L` (the track's median hip-to-knee distance). With `u` the direction along the thigh from hip to knee and `n` the normal to it that points up the frame:

- top of the knee = knee + `depthKneeTopOffset`·L·n (0.24);
- hip crease = hip + `depthCreaseAlong`·L·u + `depthCreaseNormal`·L·n (0.25 and 0.20).

The margin is the crease's depth below the top of the knee, `m = (creaseY − kneeTopY) / L`. It is positive when the crease is below the knee (image y points down), and a squat passes when `m > 0`.

`DepthAssess` works out the margin in every frame, smooths it like the hip height (`TrackSmooth`), and moves each squat's bottom to the frame where the smoothed margin is largest. Its uncertainty, as a standard deviation in thigh lengths, combines three sources:

- `depthModelDeviation` (0.07), the pose model's landmark error;
- the jitter, the root mean square of the raw margin minus the smoothed one over `depthWindow` (5) frames either side of the bottom;
- a score term, `depthScoreDeviation`·(1 − s)/(1 − `poseMinScore`), where `s` is the lower of the hip's and knee's scores, averaged over the same frames. It is 0.08 at a barely usable score and 0 at a perfect one.

With `σ` their root sum of squares, the probability the squat was to depth is `p = Φ(m/σ)`, the normal distribution's cumulative probability. The report gives the confidence in the verdict, `max(p, 1 − p)`, rounded down to a whole percentage and capped at 99%. The thigh's angle to horizontal at the bottom is reported too, negative with the hip joint below the knee joint.

### Calibration

The offsets and `depthModelDeviation` come from the bottom frames of the two sample videos. Neither has a known verdict. In each frame, brightness profiles taken straight up through the knee and crease estimates find the top edge of the leg to about a pixel:

| | Top of knee above knee keypoint | Top of thigh at crease above hip keypoint |
|---|---|---|
| 113127, frame 149 | 23 px (0.29 L) | 14 px (0.18 L) |
| 113603, frame 191 | 12–16 px (0.15–0.20 L) | 17–18 px (0.22 L) |

The offsets are the averages. Measured edge to edge, both squats bottom out level to within 2 pixels. The keypoints, with the averaged offsets, put them 0.05 above and 0.06 below, so the model's error is about 0.06 thigh lengths. It is rounded up to 0.07 because two samples are few. Both videos come out near 70–77% confident, which fits squats this close to the line.

Finding the top of the knee in the frame itself, rather than placing it from the keypoint, was tried on the same profiles and rejected. In 113603 the dark knee sleeve against the dark plate leaves no edge, and in 113127 a highlight 6 pixels below the edge would be found first.

Two more videos of the same lifter, `signal-2026-10-25-113106` and `…121233`, were added after the calibration and not used to set it. Both are shallower: the thigh is 7–8° above horizontal at the bottom, and the top of the thigh visibly slopes down from hip to knee. Both come out FAIL, at 77% and 74%, with the drawn crease above the drawn top of the knee.

Videos with known verdicts would test these values properly. None were available. `TRAIN.md` describes how to fit the knee offset and the model error to them when there are some, and `scripts/train.sh` automates it.

## Bottom-frame images

`ReportDrawBottom` draws on the frame where the smoothed margin is largest. The thigh line and the two landmarks come from that frame's keypoints, unsmoothed, so in a squat within a pixel or so of the line, the drawn crease can sit on the other side of the dashed line from the verdict, which uses the smoothed margin. Line widths and the marker's size are scaled to the thigh's length, and the label is drawn in the top left corner at twice the font's size, on a black box.

## Conventions

### Build

`build.sh` compiles every `src/*.c` file in one gcc invocation with `-std=c23` and these warnings, any of which fails the build:

```
-Wall -Wextra -Wpedantic -Werror
-Wconversion -Wsign-conversion -Wshadow
-Wformat=2 -Wformat-nonliteral
-Wjump-misses-init -Wstrict-prototypes -Wmissing-prototypes
-Wvla -Wnull-dereference
```

External libraries are found with `pkg-config` and listed in the `packages` array in `build.sh`. Their include directories are passed with `-isystem`, so the warnings apply only to our code: onnx runtime's header fails `-Wpedantic`.

### Tests

Each `tests/*_test.c` is its own program. It is linked against every `src/*.c` file except `main.c`, built with the sanitisers, and exits non-zero on failure. `./build.sh test` checks the formatting first, including STYLE.md's rule against braceless bodies on the next line (which clang-format would otherwise produce), and then runs them all.

### Vendored code

There is none. Dependencies are system libraries found through `pkg-config`, and the pose model is downloaded into `models/`, which is not committed.
