# Training squat-depth with known verdicts

How to make squat-depth's verdicts and confidence more accurate by giving it squats whose verdicts you know. `ReadMe.md` covers using the program, and `ARCHITECTURE.md` how it works.

## What training changes

The pose model, RTMPose, is used as it is. Training doesn't retrain it. What training tunes is the small calibration on top of it, in `src/settings.h`, that turns the model's hip and knee positions into a verdict.

For each squat the program works out a **margin**: how far its estimate of the hip crease is below its estimate of the top of the knee, in thigh lengths. It also works out **σ**, how uncertain that margin is. The verdict is PASS when the margin is above 0, and the confidence comes from how many σ the margin is from 0. Three things can be wrong with that:

1. **The offsets are biased.** The top of the knee is placed `depthKneeTopOffset` (0.24) above the knee keypoint, and the crease `depthCreaseNormal` (0.20) above the hip keypoint. With the thigh near level, the margin moves by about their difference. If that difference is off, the program leans towards passing or failing. Verdicts can only measure the difference, not each offset, so training adjusts `depthKneeTopOffset` and leaves the crease where it is.
2. **σ is wrong.** Its main part is `depthModelDeviation` (0.07), the pose model's error in placing the landmarks. Too small and the program is overconfident; too large and everything drifts towards 50%.
3. **Some situations are systematically harder,** such as a lower camera, baggy shorts, or a different build. Enough varied footage shows these up as groups of squats the program gets wrong. Training reports them, but a fixed offset can't correct them.

## Collecting verdicts

- **Judge from the raw video, not from `--images`.** The drawn lines would sway your judgement. A judge's call from a meet (the three-referee majority) is best. Your own careful frame-by-frame call is next best.
- **Record how sure you are** as well as pass or fail: `clear` or `borderline`.
- **Borderline squats are the most valuable.** A clear pass with a margin of +0.3 says almost nothing about the calibration. Squats within about ±0.1 of the line pin down both the bias and σ.
- **Vary everything else:** lifters, clothing, knee sleeves, gyms, camera distances and phones. Videos of one lifter in one gym can't separate "this lifter" from "this method".
- **How many:** about 20–30 labelled squats, many of them borderline, give a rough bias. Pinning down the model error takes about 50–100. Training suggests nothing from fewer than 10.

## The manifest

List the squats in a text file, one per line. `tests/videos.txt` is the default place:

```
# path                                      squat  verdict  certainty
~/Videos/meet-2027-03/lifter4-attempt2.mp4  1      fail     borderline
~/Videos/meet-2027-03/lifter4-attempt3.mp4  1      pass     clear
gym/2027-04-02 heavy single.mp4             1      pass
gym/2027-04-02 triple.mp4                   3      fail     borderline
```

- **path:** the video. `~` is expanded, and a relative path is taken from the manifest's directory. It may contain spaces, because the other fields are read from the end of the line.
- **squat:** which squat in the video, counting from 1, as the program numbers them. Run the program on the video first if you're not sure which squats it finds. A line naming a squat it doesn't find is skipped and reported.
- **verdict:** `pass` or `fail`.
- **certainty:** `clear` or `borderline`. It's optional and doesn't change the fit, but a squat marked clear that the program judges the other way is listed as worth watching. That's usually a tracking problem rather than a calibration one.

Blank lines and lines starting with `#` are ignored.

## Running training

```sh
scripts/train.sh                     # fit, and report
scripts/train.sh --apply             # fit, write the suggested settings, rebuild and test
scripts/train.sh other/manifest.txt  # a different manifest
```

`scripts/train.sh` builds the program and runs `build/squat-depth --train MANIFEST`, saving the report to `build/train-report.txt`. Each video is analysed once, which takes a few seconds per video. A video that can't be analysed is reported and left out. With `--apply`, the script then:

1. copies `src/settings.h` to `build/settings.h.before`;
2. replaces the `depthKneeTopOffset` and `depthModelDeviation` lines with the suggested ones, and shows the change;
3. rebuilds and runs the tests.

The change is an ordinary edit to `src/settings.h`. Review it with `git diff`, update the comments above the two settings to say where the values came from (the manifest, and the number of squats), and commit it with the manifest.

## Reading the report

```
12 labelled squats judged

  video                  squat  verdict           now                      fitted
  lifter4-attempt2.mp4       1  fail, borderline  PASS 61%  +0.024  ✗   FAIL 55%  -0.009  ✓
  ...

                   now    fitted  held out
  right          10/12     11/12     10/12
  log loss       0.471     0.402     0.455
```

- **The squat table** shows how each squat is judged with the settings as they are now, and with the fitted settings: the verdict, the confidence, the margin, and ✓ when it matches your verdict.
- **right** counts the matches. **log loss** scores the probabilities, and is lower when the program is confident and right, and much higher when it is confident and wrong. It is the better measure of the two, since it rewards honest confidence, not just the verdict.
- **held out** judges each video with settings fitted to all the other videos. This is the fair test of the fit. If held out is much worse than fitted, the fit is following the quirks of these particular squats and needs more of them.
- **The confidence table** checks that the confidence means what it says. For each band, *expected* is how many the program's confidence says it got right, and *right* is how many it did. Right well above expected means σ is too large; well below means too small.
- **The fit** gives each fitted value with the range the verdicts make likely, about 95%. A wide range means the verdicts don't pin that value down yet, usually because there are too few borderline squats. A fit at the edge of the range searched is flagged.
- **Marked clear but judged the other way** lists squats worth watching. Run the program on the video with `--images` and see whether the hip and knee were tracked.

## How the fit works

Training searches a grid of values for `depthKneeTopOffset` (0 to 0.6, in steps of 0.005) and `depthModelDeviation` (0.01 to 0.3, in steps of 0.0025). For each value of the knee offset it judges every labelled squat again, exactly as the program would with that setting, including finding each squat's bottom. For each pair of values it adds up the log of the probability the program gives your verdicts, and picks the pair that makes them most likely.

On its own that would be thrown off by a few squats, or by verdicts that a threshold separates perfectly, where the most likely model error is 0. So the fit starts from what the sample videos measured. It assumes the knee offset is 0.24 give or take 0.10, and the model error 0.07 within a factor of about 1.6. This prior lives in `settings.h` as the `trainPrior...` values and stays fixed, so running training again on the same verdicts gives the same answer. With a few verdicts the fit stays near those values, and with many it follows the verdicts.

## Better than verdicts: marking the landmarks

A verdict is one bit per squat. Marking where the top of the knee and the hip crease actually are on each bottom frame tells you far more: about ten marked frames are worth about fifty verdicts. It measures each offset separately, and the model's error directly. This is how the current values were set (`ARCHITECTURE.md`, Calibration). The `--images` and `--debug-csv` output make it quicker: compare the drawn points with the leg's edges, or read the keypoints from the csv. Training doesn't automate this yet.

## What training won't fix

- **A camera that isn't side-on at about hip height** distorts the geometry in a way no fixed offset corrects. Leave such videos out of the manifest rather than letting them pull the fit.
- **The pose model's own keypoint errors,** for example with a rack upright in front of the knee, can only be allowed for in σ, not removed. Improving the model would mean retraining RTMPose with MMPose on thousands of frames annotated with crease and knee-top points. That is possible, but it is a much bigger project.
