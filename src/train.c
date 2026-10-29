#include "train.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "memory.h"
#include "path.h"
#include "report.h"
#include "settings.h"

static bool
IsSpace(char character)
{
  return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

// the last word of the text from start to *end, with its length in length (0 when there is
// none). *end is moved back to the word's start.
static const char *
TakeLastWord(const char *start, const char **end, u64 *length)
{
  const char *wordEnd = *end;
  while (wordEnd > start && IsSpace(wordEnd[-1])) wordEnd--;
  const char *wordStart = wordEnd;
  while (wordStart > start && !IsSpace(wordStart[-1])) wordStart--;
  *length = (u64)(wordEnd - wordStart);
  *end = wordStart;
  return wordStart;
}

// whether the length bytes of word are expected, ignoring case.
static bool
WordIs(const char *word, u64 length, const char *expected)
{
  return strlen(expected) == length && strncasecmp(word, expected, length) == 0;
}

// the index of path in the manifest's videos, adding it when it is new.
static u64
FindVideo(TrainManifest *manifest, const char *path)
{
  for (u64 index = 0; index < manifest->videoCount; index++) {
    if (strcmp(manifest->videos[index], path) == 0) return index;
  }
  if (manifest->videoCount == manifest->videoCapacity) {
    manifest->videoCapacity = manifest->videoCapacity ? manifest->videoCapacity * 2 : 16;
    manifest->videos = MemoryResize(manifest->videos, manifest->videoCapacity, sizeof(char *));
  }
  u64 length = strlen(path);
  char *copy = MemoryResize(nullptr, length + 1, sizeof(char));
  memcpy(copy, path, length + 1);
  manifest->videos[manifest->videoCount] = copy;
  return manifest->videoCount++;
}

static constexpr char lineFormat[] = "PATH SQUAT pass|fail [clear|borderline]";

bool
TrainParseLine(
  const char *line,
  u64 number,
  const char *directory,
  TrainManifest *manifest,
  StrBuf *error
)
{
  const char *start = line;
  while (IsSpace(*start)) start++;
  if (!*start || *start == '#') return true;

  // the words are read from the end, so the path may contain spaces.
  const char *end = start + strlen(start);
  u64 length = 0;
  const char *word = TakeLastWord(start, &end, &length);
  TrainLabel label = {.line = number};
  if (WordIs(word, length, "clear")) label.certainty = TrainCertaintyClear;
  else if (WordIs(word, length, "borderline")) label.certainty = TrainCertaintyBorderline;
  if (label.certainty != TrainCertaintyUnstated) word = TakeLastWord(start, &end, &length);

  if (WordIs(word, length, "pass")) label.passed = true;
  else if (!WordIs(word, length, "fail")) {
    StrBufAppendFormat(error, "expected pass or fail, as in \"%s\"", lineFormat);
    return false;
  }

  word = TakeLastWord(start, &end, &length);
  for (u64 index = 0; index < length && length <= 9; index++) {
    if (word[index] < '0' || word[index] > '9') length = 0;
    else label.squat = label.squat * 10 + (u64)(word[index] - '0');
  }
  if (!length || length > 9 || !label.squat) {
    StrBufAppendFormat(error, "expected a squat number from 1, as in \"%s\"", lineFormat);
    return false;
  }

  while (end > start && IsSpace(end[-1])) end--;
  if (end == start) {
    StrBufAppendFormat(error, "expected the video's path first, as in \"%s\"", lineFormat);
    return false;
  }
  StrBuf given = {};
  StrBuf path = {};
  StrBufAppendBytes(&given, start, (u64)(end - start));
  const char *text = StrBufText(&given);
  if (text[0] != '/' && text[0] != '~' && directory && *directory) {
    StrBufAppendFormat(&path, "%s/", directory);
  }
  PathExpandHome(text, &path);
  label.video = FindVideo(manifest, StrBufText(&path));
  StrBufFree(&given);
  StrBufFree(&path);

  if (manifest->labelCount == manifest->labelCapacity) {
    manifest->labelCapacity = manifest->labelCapacity ? manifest->labelCapacity * 2 : 16;
    manifest->labels =
      MemoryResize(manifest->labels, manifest->labelCapacity, sizeof(TrainLabel));
  }
  manifest->labels[manifest->labelCount++] = label;
  return true;
}

bool
TrainReadManifest(const char *path, TrainManifest *manifest, StrBuf *error)
{
  FILE *file = fopen(path, "r");
  if (!file) {
    StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
    return false;
  }

  // video paths are relative to the manifest's directory.
  StrBuf directory = {};
  const char *slash = strrchr(path, '/');
  if (slash) StrBufAppendBytes(&directory, path, (u64)(slash - path));
  StrBuf line = {};
  StrBuf reason = {};
  bool parsed = true;
  for (u64 number = 1; parsed && StrBufReadLine(file, &line); number++) {
    parsed =
      TrainParseLine(StrBufText(&line), number, StrBufText(&directory), manifest, &reason);
    if (!parsed) {
      StrBufAppendFormat(error, "%s:%" PRIu64 ": %s", path, number, StrBufText(&reason));
    }
  }
  if (parsed && ferror(file)) {
    StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
    parsed = false;
  }
  if (parsed && !manifest->labelCount) {
    StrBufAppendFormat(error, "%s: no labelled squats in it", path);
    parsed = false;
  }
  fclose(file);
  StrBufFree(&directory);
  StrBufFree(&line);
  StrBufFree(&reason);
  return parsed;
}

void
TrainManifestFree(TrainManifest *manifest)
{
  for (u64 index = 0; index < manifest->videoCount; index++) free(manifest->videos[index]);
  free(manifest->videos);
  free(manifest->labels);
  *manifest = (TrainManifest){};
}

// the count of values from lowest to highest in steps of step.
static u64
GridSize(f64 lowest, f64 highest, f64 step)
{
  return (u64)llround((highest - lowest) / step) + 1;
}

// the log of the chance of the known verdict, given the program's probability of a pass.
static f64
LogChance(bool passed, f64 probability)
{
  return log(fmax(passed ? probability : 1 - probability, 1e-12));
}

// how far a calibration is from what training believes before seeing verdicts, as a log
// probability.
static f64
LogPrior(f64 kneeTop, f64 deviation)
{
  f64 knee = (kneeTop - trainPriorKneeTopOffset) / trainPriorKneeTopSpread;
  f64 spread = log(deviation / trainPriorModelDeviation) / trainPriorDeviationSpread;
  return -(knee * knee + spread * spread) / 2;
}

// one usable label's squat, judged one way.
typedef struct Judgement {
  f64 margin;
  f64 probability;
} Judgement;

// a way of judging every usable label, and how it did.
typedef struct Outcome {
  u64 right;
  f64 logLoss;

  // per confidence band: the squats, the sum of their confidences, and those judged right.
  u64 bandSquats[3];
  f64 bandExpected[3];
  u64 bandRight[3];
} Outcome;

// the confidence bands' lower bounds, as fractions.
static const f64 bandLowest[3] = {0.5, 0.7, 0.9};
static const char *const bandNames[3] = {"50–69%", "70–89%", "90–99%"};

// adds a judgement of a squat whose known verdict is passed to outcome.
static void
Tally(Outcome *outcome, bool passed, Judgement judgement)
{
  bool right = (judgement.margin > 0) == passed;
  f64 confidence = fmax(judgement.probability, 1 - judgement.probability);
  u64 band = confidence >= bandLowest[2] ? 2 : confidence >= bandLowest[1] ? 1 : 0;
  outcome->right += right;
  outcome->logLoss -= LogChance(passed, judgement.probability);
  outcome->bandSquats[band]++;
  outcome->bandExpected[band] += confidence;
  outcome->bandRight[band] += right;
}

// appends a judgement as verdict, confidence, margin and whether it was right.
static void
AppendJudgement(StrBuf *report, bool passed, Judgement judgement)
{
  bool judgedPass = judgement.margin > 0;
  StrBufAppendFormat(
    report,
    "%s %2" PRIu32 "%%  %+.3f  %s",
    judgedPass ? "PASS" : "FAIL",
    ReportConfidence(judgement.probability),
    judgement.margin,
    judgedPass == passed ? "✓" : "✗"
  );
}

static const char *
CertaintyName(enum TrainCertainty certainty)
{
  return certainty == TrainCertaintyClear   ? ", clear"
    : certainty == TrainCertaintyBorderline ? ", borderline"
                                            : "";
}

bool
TrainFit(
  const TrainManifest *manifest,
  Track *tracks,
  const bool *tracked,
  TrainResult *result,
  StrBuf *report
)
{
  *result = (TrainResult){};
  DepthCalibration current = DepthDefaultCalibration();
  u64 kneeCount = GridSize(trainKneeTopLowest, trainKneeTopHighest, trainKneeTopStep);
  u64 deviationCount =
    GridSize(trainDeviationLowest, trainDeviationHighest, trainDeviationStep);
  u64 gridSize = kneeCount * deviationCount;

  // a label is usable when its video was tracked and has that many squats.
  u64 labelCount = manifest->labelCount;
  u64 *usable = MemoryResize(nullptr, labelCount, sizeof(u64));
  u64 usableCount = 0;
  for (u64 index = 0; index < labelCount; index++) {
    const TrainLabel *label = &manifest->labels[index];
    const char *name = PathFileName(manifest->videos[label->video]);
    if (!tracked[label->video]) {
      StrBufAppendFormat(
        report,
        "skipped line %" PRIu64 ": %s could not be analysed\n",
        label->line,
        name
      );
    } else if (label->squat > tracks[label->video].repCount) {
      StrBufAppendFormat(
        report,
        "skipped line %" PRIu64 ": squat %" PRIu64 " of %s, which has %" PRIu64
        " squat%s as the program counts them\n",
        label->line,
        label->squat,
        name,
        tracks[label->video].repCount,
        tracks[label->video].repCount == 1 ? "" : "s"
      );
    } else {
      usable[usableCount++] = index;
    }
  }
  result->usable = usableCount;
  if (!usableCount) {
    free(usable);
    return false;
  }

  // judges every usable squat now, and with each candidate top of the knee. the model's
  // error only changes the probability, so the squats' margins and other errors are kept.
  Judgement *now = MemoryResize(nullptr, usableCount, sizeof(Judgement));
  f64 *margins = MemoryResize(nullptr, usableCount * kneeCount, sizeof(f64));
  f64 *noises = MemoryResize(nullptr, usableCount * kneeCount, sizeof(f64));
  Depth depths[maxReps];
  for (u64 knee = 0; knee <= kneeCount; knee++) {
    DepthCalibration calibration = current;
    if (knee < kneeCount) {
      calibration.kneeTopOffset = trainKneeTopLowest + (f64)knee * trainKneeTopStep;
    }
    for (u64 video = 0; video < manifest->videoCount; video++) {
      if (!tracked[video] || !tracks[video].repCount) continue;
      DepthAssess(&tracks[video], &calibration, depths);
      for (u64 index = 0; index < usableCount; index++) {
        const TrainLabel *label = &manifest->labels[usable[index]];
        if (label->video != video) continue;
        const Depth *depth = &depths[label->squat - 1];
        if (knee == kneeCount) {
          now[index] = (Judgement){depth->margin, depth->probability};
          continue;
        }
        margins[index * kneeCount + knee] = depth->margin;
        noises[index * kneeCount + knee] = depth->jitterDeviation * depth->jitterDeviation +
          depth->scoreDeviation * depth->scoreDeviation;
      }
    }
  }

  // the log likelihood of each video's verdicts at every point of the grid, and the total
  // with the prior.
  u64 videoCount = manifest->videoCount;
  f64 *videoLikelihood = MemoryResize(nullptr, videoCount * gridSize, sizeof(f64));
  f64 *total = MemoryResize(nullptr, gridSize, sizeof(f64));
  memset(videoLikelihood, 0, videoCount * gridSize * sizeof(f64));
  for (u64 knee = 0; knee < kneeCount; knee++) {
    for (u64 step = 0; step < deviationCount; step++) {
      f64 deviation = trainDeviationLowest + (f64)step * trainDeviationStep;
      u64 point = knee * deviationCount + step;
      for (u64 index = 0; index < usableCount; index++) {
        const TrainLabel *label = &manifest->labels[usable[index]];
        f64 spread = sqrt(deviation * deviation + noises[index * kneeCount + knee]);
        f64 probability = DepthProbability(margins[index * kneeCount + knee], spread);
        videoLikelihood[label->video * gridSize + point] +=
          LogChance(label->passed, probability);
      }
      total[point] = LogPrior(trainKneeTopLowest + (f64)knee * trainKneeTopStep, deviation);
      for (u64 video = 0; video < videoCount; video++) {
        total[point] += videoLikelihood[video * gridSize + point];
      }
    }
  }

  // the best point, and the range of each value at points nearly as likely.
  u64 best = 0;
  for (u64 point = 1; point < gridSize; point++) {
    if (total[point] > total[best]) best = point;
  }
  u64 bestKnee = best / deviationCount;
  u64 bestStep = best % deviationCount;
  result->fitted = current;
  result->fitted.kneeTopOffset = trainKneeTopLowest + (f64)bestKnee * trainKneeTopStep;
  result->fitted.modelDeviation = trainDeviationLowest + (f64)bestStep * trainDeviationStep;
  result->kneeTopLow = INFINITY;
  result->kneeTopHigh = -INFINITY;
  result->deviationLow = INFINITY;
  result->deviationHigh = -INFINITY;
  for (u64 point = 0; point < gridSize; point++) {
    if (total[point] < total[best] - trainLikelyDrop) continue;
    f64 kneeTop = trainKneeTopLowest + (f64)(point / deviationCount) * trainKneeTopStep;
    f64 deviation = trainDeviationLowest + (f64)(point % deviationCount) * trainDeviationStep;
    result->kneeTopLow = fmin(result->kneeTopLow, kneeTop);
    result->kneeTopHigh = fmax(result->kneeTopHigh, kneeTop);
    result->deviationLow = fmin(result->deviationLow, deviation);
    result->deviationHigh = fmax(result->deviationHigh, deviation);
  }
  result->suggested = usableCount >= trainMinSquats;

  // judges each squat with the best fit, and with the best fit to the other videos only.
  Judgement *fitted = MemoryResize(nullptr, usableCount, sizeof(Judgement));
  Judgement *heldOut = MemoryResize(nullptr, usableCount, sizeof(Judgement));
  for (u64 index = 0; index < usableCount; index++) {
    const TrainLabel *label = &manifest->labels[usable[index]];
    u64 others = 0;
    for (u64 point = 1; point < gridSize; point++) {
      f64 here = total[point] - videoLikelihood[label->video * gridSize + point];
      f64 there = total[others] - videoLikelihood[label->video * gridSize + others];
      if (here > there) others = point;
    }
    u64 points[2] = {best, others};
    Judgement *judgements[2] = {fitted, heldOut};
    for (u64 which = 0; which < 2; which++) {
      u64 knee = points[which] / deviationCount;
      f64 deviation = trainDeviationLowest +
        (f64)(points[which] % deviationCount) * trainDeviationStep;
      f64 margin = margins[index * kneeCount + knee];
      f64 spread = sqrt(deviation * deviation + noises[index * kneeCount + knee]);
      judgements[which][index] = (Judgement){margin, DepthProbability(margin, spread)};
    }
  }

  // the report: each squat, how each way of judging did, the fit, and what to check.
  u64 nameWidth = strlen("video");
  for (u64 index = 0; index < usableCount; index++) {
    const TrainLabel *label = &manifest->labels[usable[index]];
    u64 length = strlen(PathFileName(manifest->videos[label->video]));
    if (length > nameWidth) nameWidth = length;
  }
  StrBufAppendFormat(
    report,
    "%" PRIu64
    " labelled squat%s judged\n\n  %-*s  squat  verdict           now                  "
    "    fitted\n",
    usableCount,
    usableCount == 1 ? "" : "s",
    (int)nameWidth,
    "video"
  );
  Outcome outcomes[3] = {};
  for (u64 index = 0; index < usableCount; index++) {
    const TrainLabel *label = &manifest->labels[usable[index]];
    StrBuf verdict = {};
    StrBufAppendFormat(
      &verdict,
      "%s%s",
      label->passed ? "pass" : "fail",
      CertaintyName(label->certainty)
    );
    StrBufAppendFormat(
      report,
      "  %-*s  %5" PRIu64 "  %-16s  ",
      (int)nameWidth,
      PathFileName(manifest->videos[label->video]),
      label->squat,
      StrBufText(&verdict)
    );
    StrBufFree(&verdict);
    AppendJudgement(report, label->passed, now[index]);
    StrBufAppend(report, "   ");
    AppendJudgement(report, label->passed, fitted[index]);
    StrBufAppend(report, "\n");
    Tally(&outcomes[0], label->passed, now[index]);
    Tally(&outcomes[1], label->passed, fitted[index]);
    Tally(&outcomes[2], label->passed, heldOut[index]);
  }

  StrBufAppendFormat(
    report,
    "\n  %-10s%10s%10s%10s\n  %-10s",
    "",
    "now",
    "fitted",
    "held out",
    "right"
  );
  for (u64 which = 0; which < 3; which++) {
    char right[32];
    snprintf(right, sizeof(right), "%" PRIu64 "/%" PRIu64, outcomes[which].right, usableCount);
    StrBufAppendFormat(report, "%10s", right);
  }
  StrBufAppendFormat(report, "\n  %-10s", "log loss");
  for (u64 which = 0; which < 3; which++) {
    StrBufAppendFormat(report, "%10.3f", outcomes[which].logLoss / (f64)usableCount);
  }
  StrBufAppend(
    report,
    "\n  held out judges each video with a fit to the other videos only, a fairer test of the "
    "fit.\n  log loss is lower when the program is confident and right.\n\n"
  );

  // a calibrated program is right about as often as its confidence says.
  StrBufAppendFormat(report, "  %-10s%25s%25s\n  %-10s", "", "now", "fitted", "confidence");
  for (u64 which = 0; which < 2; which++) {
    StrBufAppendFormat(report, "%8s%10s%7s", "squats", "expected", "right");
  }
  StrBufAppend(report, "\n");
  for (u64 band = 0; band < 3; band++) {
    // the band names are 6 characters, which the en dash makes 8 bytes.
    StrBufAppendFormat(report, "  %s    ", bandNames[band]);
    for (u64 which = 0; which < 2; which++) {
      StrBufAppendFormat(
        report,
        "%8" PRIu64 "%10.1f%7" PRIu64,
        outcomes[which].bandSquats[band],
        outcomes[which].bandExpected[band],
        outcomes[which].bandRight[band]
      );
    }
    StrBufAppend(report, "\n");
  }
  StrBufAppend(report, "  expected is how many the program's confidence says it got right.\n");

  StrBufAppendFormat(
    report,
    "\nfit, with the range the verdicts make likely:\n"
    "  top of the knee (depthKneeTopOffset)  now %.3f  fitted %.3f  likely %.3f to %.3f\n"
    "  model error (depthModelDeviation)     now %.3f  fitted %.3f  likely %.3f to %.3f\n",
    current.kneeTopOffset,
    result->fitted.kneeTopOffset,
    result->kneeTopLow,
    result->kneeTopHigh,
    current.modelDeviation,
    result->fitted.modelDeviation,
    result->deviationLow,
    result->deviationHigh
  );
  if (bestKnee == 0 || bestKnee == kneeCount - 1 || bestStep == 0 ||
    bestStep == deviationCount - 1) {
    StrBufAppend(
      report,
      "  the fit is at the edge of the range searched, so look at the squats before trusting "
      "it\n"
    );
  }

  bool checking = false;
  for (u64 index = 0; index < usableCount; index++) {
    const TrainLabel *label = &manifest->labels[usable[index]];
    if (label->certainty != TrainCertaintyClear) continue;
    if ((fitted[index].margin > 0) == label->passed) continue;
    if (!checking) {
      StrBufAppend(
        report,
        "\nmarked clear but judged the other way, so worth watching (--images draws the "
        "bottom):\n"
      );
      checking = true;
    }
    StrBufAppendFormat(
      report,
      "  line %" PRIu64 ": %s squat %" PRIu64 ", a clear %s\n",
      label->line,
      PathFileName(manifest->videos[label->video]),
      label->squat,
      label->passed ? "pass" : "fail"
    );
  }

  if (result->suggested) {
    StrBufAppendFormat(
      report,
      "\nsuggested settings for src/settings.h (scripts/train.sh --apply writes them):\n"
      "static constexpr f64 depthKneeTopOffset = %.3f;\n"
      "static constexpr f64 depthModelDeviation = %.4f;\n",
      result->fitted.kneeTopOffset,
      result->fitted.modelDeviation
    );
  } else {
    StrBufAppendFormat(
      report,
      "\nno settings suggested: training needs at least %" PRIu64
      " usable labelled squats, and has %" PRIu64 ".\n",
      trainMinSquats,
      usableCount
    );
  }

  free(usable);
  free(now);
  free(margins);
  free(noises);
  free(videoLikelihood);
  free(total);
  free(fitted);
  free(heldOut);
  return true;
}
