# squat-depth

Judges the depth of powerlifting squats from a side-on video. It counts the squats, and for each one decides whether the hip crease went below the top of the knee, as the IPF rules require, and says how confident it is. It can also save a picture of each squat's deepest frame with the measurement drawn on it.

```
$ build/squat-depth --images ~/Downloads/squat.mp4
squat.mp4: 1 squat
  squat 1  bottom 6.34 s  thigh -2.2°  margin +0.06  PASS  77% confident this was a good lift
  wrote squat-squat-1.png
```

**What it is not:** a judge. It estimates depth from a 2D pose model's guess at where the hip and knee are, and on a typical phone video one pixel is a sizeable fraction of the margin that decides a borderline squat. Treat its verdict as a second opinion, and its confidence as a measure of how close the squat was.

## Building

Needs:

- gcc with C23 support (gcc 14 or later), and `pkg-config`;
- the FFmpeg libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`);
- ONNX Runtime (`libonnxruntime`), which runs the pose model;
- clang-format 22 or later, for `format` and `test`.

On Arch:

```sh
sudo pacman -S gcc pkgconf ffmpeg onnxruntime-cpu clang
```

On Debian or Ubuntu, the FFmpeg libraries are `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev`. ONNX Runtime's C library comes from its GitHub releases, and needs a `libonnxruntime.pc` that `pkg-config` can find.

The pose model, RTMPose-m from OpenMMLab's MMPose (Apache-2.0, about 54 MB), isn't in the repository. Download it into `models/`, checked against its sha256, with:

```sh
scripts/fetch-model.sh
```

Then build with:

```sh
./build.sh          # optimised build, to build/squat-depth
./build.sh debug    # with address and undefined-behaviour sanitisers
./build.sh test     # formatting check and unit tests
./build.sh format   # format src/ and tests/ in place
```

## Usage

```sh
build/squat-depth
Video file: ~/Downloads/squat.mp4
```

You can also give the file name as an argument: `build/squat-depth squat.mp4`. A leading `~` is expanded in both cases. Any format FFmpeg reads will do, and phone videos stored sideways are turned upright.

### Reading the report

```
signal-2026-10-28-113127.mp4: 1 squat
  squat 1  bottom 4.96 s  thigh +6.0°  margin -0.05  FAIL  70% confident this was not to depth
```

For each squat:

- **bottom** is the time of the deepest frame: the one where the hip crease is furthest below the top of the knee;
- **thigh** is the thigh's angle to horizontal there, from hip joint to knee joint, negative when the hip joint is below the knee joint. It is a familiar way to describe depth, but the verdict doesn't use it: the rule is about the surface of the leg, not the joints;
- **margin** is how far the hip crease was below the top of the knee, in thigh lengths, negative when it was above. On a typical thigh of 45 cm, 0.05 is about 2 cm. A squat passes when the margin is above 0;
- **PASS** or **FAIL**, and the **confidence** that this verdict is right, given how uncertain the margin is. It is never more than 99%. Near 50% means the squat was too close to call from the video; above 90% means the pose model would have to be badly wrong for the verdict to change.

A squat still under way when the video ends is judged, and flagged. A video with no squats in it reports 0 squats. The program exits with 0 when it has judged the video, and with 1 and a one-line message on stderr when it can't: the file is missing or isn't a video, the pose model is missing, or no lifter was found.

### Options

| Option | Effect |
|---|---|
| `--images` | writes each squat's deepest frame to `<video name>-squat-<n>.png` in the current directory. On it are the thigh from hip to knee (white), a dashed line at the estimated top of the knee (yellow), a marker at the estimated hip crease, and the thigh angle, verdict and confidence, in green for a pass and red for a fail |
| `--details` | adds the working behind each verdict: the frames and thigh length used, when each squat started and ended, the bottom frame's number, and the margin's standard deviation and the three parts it is made of |
| `--train MANIFEST` | fits the depth settings to squats with known verdicts listed in the manifest, instead of judging one video. `TRAIN.md` explains it, and `scripts/train.sh` runs it |
| `--model PATH` | the pose model to use, instead of `models/rtmpose-m.onnx` |
| `--debug-csv PATH` | writes each frame's time, the smoothed hip height used to find squats, the unsmoothed depth margin, and keypoints (x, y and score for all 17 COCO keypoints) as csv |
| `--dump-frames N` | writes every Nth frame, upright, to `frames/frame-NNNN.png`, with the pose drawn on it: the box the lifter was looked for in, the left side in blue, the right in orange, and keypoints green when found and red when not |

## How it works

```
video ─► decode + rotate ─► pose in each frame ─► find squats ─► judge depth ─► report, images
```

1. **Decode.** FFmpeg decodes each frame, and it is turned upright using the rotation the phone stored with the video.
2. **Pose.** RTMPose, run by ONNX Runtime, finds the 17 COCO body keypoints in each frame. It looks around where the lifter was in the frame before, and searches the whole frame again when it loses them.
3. **Find squats.** The program uses the hip and knee on the side facing the camera. It follows the hip's height above the knee, divided by the thigh's length: about 1 standing, 0 with the hip joint level with the knee joint. A squat is a drop of at least 0.5 from standing and a return to near it. That ignores the walk-out and re-rack, which dip much less.
4. **Judge depth.** The rule is about surfaces the model doesn't find, so the program places them from the joints it does, in thigh lengths `L` along the thigh and up from it:
   - top of the knee: 0.24 `L` above the knee joint;
   - hip crease: 0.25 `L` in front of the hip keypoint and 0.20 `L` above it.

   The margin is the height of the crease below the top of the knee, divided by `L`. The squat's bottom is the frame where that margin, smoothed, is largest.
5. **Confidence.** The margin's uncertainty is a standard deviation made of three parts: the pose model's error in placing the landmarks (0.07 `L`), how much the margin jitters from frame to frame around the bottom, and extra doubt when the model scores the hip or knee low. The probability that the squat was to depth is the normal distribution's chance that the true margin is above 0, and the confidence is that probability, or one minus it for a fail.

The offsets and the model's error were measured on the deepest frames of two sample videos, and checked on two more. `ARCHITECTURE.md` has the measurements.

## Limitations and filming tips

- **Film from the side, at about hip height,** square on to the lifter, with the camera still. An angled or high camera changes where the hip and knee appear relative to each other.
- **Keep the whole lifter in frame,** from head to feet, through the whole set.
- **Keep the near knee and hip clear.** A rack upright or a plate in front of them makes the model less sure, and lowers the confidence.
- **Use the best resolution you can,** 720p or more. On the 640×360 sample videos the thigh is under 80 pixels long, so one pixel is more than 0.01 of the margin.
- **One lifter.** The program follows one person and isn't built for a crowded frame: a spotter standing between the lifter and the camera can confuse it.
- **Calibration is thin.** It rests on four videos of one lifter in one gym, none with a known verdict. Loose clothing, a different build, or very different knee sleeves may need different offsets. They are all in `src/settings.h`, and `TRAIN.md` explains how to tune them with videos whose verdicts you know.

## Licences

- FFmpeg: LGPL 2.1 or later, or GPL when it is built with GPL components. Arch's build is GPL 3, so a binary linked against it, and given to others, falls under GPL 3.
- ONNX Runtime: MIT.
- RTMPose-m model weights: Apache-2.0, from OpenMMLab's MMPose.

## Further reading

- `ARCHITECTURE.md`: the modules, the data flow, how each stage works, the calibration, and the build conventions.
- `TRAIN.md`: improving the calibration with squats whose verdicts you know.
- `STYLE.md`: how the code is written.
