#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "depth.h"
#include "image.h"
#include "media.h"
#include "memory.h"
#include "path.h"
#include "pose.h"
#include "report.h"
#include "settings.h"
#include "strbuf.h"
#include "track.h"
#include "train.h"
#include "types.h"
#include "video.h"

// appends text to trimmed without its leading and trailing whitespace.
static void
Trim(const char *text, StrBuf *trimmed)
{
  const char *start = text;
  while (*start == ' ' || *start == '\t' || *start == '\r') start++;
  const char *end = start + strlen(start);
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
  StrBufAppendBytes(trimmed, start, (u64)(end - start));
}

// asks for the video's file name on stdout and reads it from stdin into path.
static bool
PromptForPath(StrBuf *path, StrBuf *error)
{
  StrBuf line = {};
  StrBuf trimmed = {};
  fputs("Video file: ", stdout);
  fflush(stdout);
  bool read = StrBufReadLine(stdin, &line);
  // at end of file the cursor is still after the prompt.
  if (!read) {
    fputc('\n', stdout);
    fflush(stdout);
  }
  if (read) Trim(StrBufText(&line), &trimmed);
  if (read && trimmed.length) PathExpandHome(StrBufText(&trimmed), path);
  StrBufFree(&line);
  StrBufFree(&trimmed);

  if (!path->length) {
    StrBufAppend(error, "no video file given");
    return false;
  }
  return true;
}

// checks that path names a readable regular file.
static bool
CheckVideoFile(const char *path, StrBuf *error)
{
  struct stat status;
  if (stat(path, &status) != 0) {
    StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
    return false;
  }
  if (!S_ISREG(status.st_mode)) {
    StrBufAppendFormat(error, "%s: not a regular file", path);
    return false;
  }
  if (access(path, R_OK) != 0) {
    StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
    return false;
  }
  return true;
}

// the command line, once parsed.
typedef struct Options {
  // the video's path, or nullptr to prompt for it.
  const char *path;

  // the pose model's path.
  const char *modelPath;

  // where to write each frame's keypoints as csv, or nullptr for nowhere.
  const char *csvPath;

  // writes every dumpEvery-th frame to dumpDirectory as a png, or none when 0.
  u64 dumpEvery;

  // adds the working behind each verdict to the report.
  bool detailed;

  // writes each squat's bottom frame, with its depth drawn on it, as a png.
  bool images;

  // fits the calibration to the squats with known verdicts listed in this manifest, or
  // nullptr to judge one video.
  const char *trainPath;
} Options;

static constexpr char usage[] =
  "usage: squat-depth [--details] [--images] [--model PATH] [--debug-csv PATH] "
  "[--dump-frames N] [video.mp4]\n"
  "       squat-depth --train MANIFEST [--model PATH]";

// the value following the option at *index, which is moved past it, or nullptr with a
// reason appended to error when there is none.
static const char *
OptionValue(int argc, char **argv, int *index, StrBuf *error)
{
  if (*index + 1 >= argc) {
    StrBufAppendFormat(error, "%s needs a value", argv[*index]);
    return nullptr;
  }
  ++*index;
  return argv[*index];
}

// fills options from the command line.
static bool
ParseOptions(int argc, char **argv, Options *options, StrBuf *error)
{
  *options = (Options){.modelPath = defaultModelPath};
  for (int index = 1; index < argc; index++) {
    const char *argument = argv[index];
    const char *value = nullptr;
    if (strcmp(argument, "--dump-frames") == 0) {
      value = OptionValue(argc, argv, &index, error);
      if (!value) return false;
      char *end = nullptr;
      options->dumpEvery = strtoull(value, &end, 10);
      if (!*value || *end || !options->dumpEvery) {
        StrBufAppendFormat(
          error,
          "--dump-frames needs a whole number above 0, not \"%s\"",
          value
        );
        return false;
      }
    } else if (strcmp(argument, "--details") == 0) {
      options->detailed = true;
    } else if (strcmp(argument, "--images") == 0) {
      options->images = true;
    } else if (strcmp(argument, "--train") == 0) {
      options->trainPath = OptionValue(argc, argv, &index, error);
      if (!options->trainPath) return false;
    } else if (strcmp(argument, "--model") == 0) {
      options->modelPath = OptionValue(argc, argv, &index, error);
      if (!options->modelPath) return false;
    } else if (strcmp(argument, "--debug-csv") == 0) {
      options->csvPath = OptionValue(argc, argv, &index, error);
      if (!options->csvPath) return false;
    } else if (argument[0] == '-' && argument[1]) {
      StrBufAppendFormat(error, "unknown option %s", argument);
      return false;
    } else if (options->path) {
      StrBufAppend(error, "give only one video");
      return false;
    } else {
      options->path = argument;
    }
  }
  bool judging = options->path || options->detailed || options->images || options->csvPath ||
    options->dumpEvery;
  if (options->trainPath && judging) {
    StrBufAppend(error, "--train takes only --model, not a video or the other options");
    return false;
  }
  return true;
}

// draws the box the person was looked for in, the lines of the body and legs, and each
// keypoint coloured by whether it was found.
static void
DrawPose(Image *frame, const Pose *pose, Box box)
{
  static const struct {
    enum PoseKeypoint from;
    enum PoseKeypoint to;
    Color color;
  } bones[] = {
    {PoseLeftShoulder, PoseLeftHip, {80, 160, 255}},
    {PoseLeftHip, PoseLeftKnee, {80, 160, 255}},
    {PoseLeftKnee, PoseLeftAnkle, {80, 160, 255}},
    {PoseRightShoulder, PoseRightHip, {255, 160, 40}},
    {PoseRightHip, PoseRightKnee, {255, 160, 40}},
    {PoseRightKnee, PoseRightAnkle, {255, 160, 40}}
  };

  ImageDrawRectangle(frame, box.x, box.y, box.width, box.height, 1, (Color){255, 255, 255});
  for (u64 index = 0; index < sizeof(bones) / sizeof(bones[0]); index++) {
    const Keypoint *from = &pose->keypoints[bones[index].from];
    const Keypoint *to = &pose->keypoints[bones[index].to];
    ImageDrawLine(frame, from->x, from->y, to->x, to->y, 2, bones[index].color);
  }
  for (u32 index = PoseLeftShoulder; index < PoseKeypointCount; index++) {
    const Keypoint *keypoint = &pose->keypoints[index];
    Color color = keypoint->score >= poseMinScore ? (Color){0, 230, 0} : (Color){230, 0, 0};
    ImageDrawDisc(frame, keypoint->x, keypoint->y, 3, color);
  }
}

// the poses found in a video, one per frame.
typedef struct Clip {
  Pose *poses;
  f64 *seconds;
  u64 count;
  u64 capacity;
  VideoInfo info;
} Clip;

// adds a frame's pose and time to the clip.
static void
ClipAdd(Clip *clip, const Pose *pose, f64 seconds)
{
  if (clip->count == clip->capacity) {
    clip->capacity = clip->capacity ? clip->capacity * 2 : 256;
    clip->poses = MemoryResize(clip->poses, clip->capacity, sizeof(Pose));
    clip->seconds = MemoryResize(clip->seconds, clip->capacity, sizeof(f64));
  }
  clip->poses[clip->count] = *pose;
  clip->seconds[clip->count] = seconds;
  clip->count++;
}

static void
ClipFree(Clip *clip)
{
  free(clip->poses);
  free(clip->seconds);
  *clip = (Clip){};
}

// finds the pose in every frame of the video at path, dumping frames as options ask.
static bool
FindPoses(const char *path, const Options *options, Clip *clip, StrBuf *error)
{
  if (options->dumpEvery && mkdir(dumpDirectory, 0777) != 0 && errno != EEXIST) {
    StrBufAppendFormat(error, "%s: %s", dumpDirectory, strerror(errno));
    return false;
  }
  PoseModel *model = PoseModelOpen(options->modelPath, error);
  if (!model) return false;
  Video *video = VideoOpen(path, error);
  if (!video) {
    PoseModelClose(model);
    return false;
  }

  Image frame = {};
  StrBuf name = {};
  Pose pose = {};
  Box box = {};
  f64 seconds = 0;
  enum VideoRead read = VideoReadFrame;
  bool succeeded = true;
  while (succeeded &&
    (read = VideoNextFrame(video, &frame, &seconds, error)) == VideoReadFrame) {
    succeeded = PoseFollow(model, &frame, &pose, &box, error);
    if (!succeeded) break;
    if (options->dumpEvery && clip->count % options->dumpEvery == 0) {
      DrawPose(&frame, &pose, box);
      StrBufClear(&name);
      StrBufAppendFormat(&name, "%s/frame-%04" PRIu64 ".png", dumpDirectory, clip->count);
      succeeded = ImageWritePng(&frame, StrBufText(&name), error);
    }
    ClipAdd(clip, &pose, seconds);
  }

  clip->info = VideoGetInfo(video);
  VideoClose(video);
  PoseModelClose(model);
  ImageFree(&frame);
  StrBufFree(&name);
  return succeeded && read != VideoReadError;
}

// writes each frame's time, keypoints, and the tracked hip height and depth margin (blank
// when track is nullptr) to path as csv.
static bool
WriteCsv(const char *path, const Clip *clip, const Track *track, StrBuf *error)
{
  FILE *csv = fopen(path, "w");
  if (!csv) {
    StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
    return false;
  }

  fputs("frame,seconds,height,margin", csv);
  for (u32 index = 0; index < PoseKeypointCount; index++) {
    const char *name = PoseKeypointName(index);
    fprintf(csv, ",%s_x,%s_y,%s_score", name, name, name);
  }
  fputc('\n', csv);
  for (u64 frame = 0; frame < clip->count; frame++) {
    fprintf(csv, "%" PRIu64 ",%.4f,", frame, clip->seconds[frame]);
    if (track) {
      DepthCalibration calibration = DepthDefaultCalibration();
      const Keypoint *hip = &track->hips[frame];
      const Keypoint *knee = &track->knees[frame];
      f64 margin = DepthMeasure(*hip, *knee, track->thighLength, &calibration).margin;
      fprintf(csv, "%.4f,%.4f", track->height[frame], margin);
    } else {
      fputc(',', csv);
    }
    for (u32 index = 0; index < PoseKeypointCount; index++) {
      const Keypoint *keypoint = &clip->poses[frame].keypoints[index];
      fprintf(csv, ",%.2f,%.2f,%.3f", keypoint->x, keypoint->y, keypoint->score);
    }
    fputc('\n', csv);
  }

  if (fclose(csv) != 0) {
    StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
    return false;
  }
  return true;
}

// appends to name the file name of the image of squat number of the video at path: the
// video's file name without its extension, then -squat-number.png.
static void
ImageName(const char *path, u64 number, StrBuf *name)
{
  const char *start = PathFileName(path);
  const char *dot = strrchr(start, '.');
  u64 length = dot && dot != start ? (u64)(dot - start) : strlen(start);
  StrBufAppendBytes(name, start, length);
  StrBufAppendFormat(name, "-squat-%" PRIu64 ".png", number);
}

// decodes the video at path again and writes each squat's bottom frame, with its depth drawn
// on it, to the working directory, printing each file's name.
static bool
WriteImages(const char *path, const Track *track, const Depth *depths, StrBuf *error)
{
  Video *video = VideoOpen(path, error);
  if (!video) return false;

  u64 last = 0;
  for (u64 index = 0; index < track->repCount; index++) {
    if (depths[index].bottom > last) last = depths[index].bottom;
  }
  Image frame = {};
  StrBuf name = {};
  f64 seconds = 0;
  enum VideoRead read = VideoReadFrame;
  bool succeeded = true;
  for (u64 at = 0; succeeded && at <= last; at++) {
    read = VideoNextFrame(video, &frame, &seconds, error);
    if (read != VideoReadFrame) break;
    for (u64 index = 0; succeeded && index < track->repCount; index++) {
      if (depths[index].bottom != at) continue;
      ReportDrawBottom(&frame, track, &depths[index]);
      StrBufClear(&name);
      ImageName(path, index + 1, &name);
      succeeded = ImageWritePng(&frame, StrBufText(&name), error);
      if (succeeded) printf("  wrote %s\n", StrBufText(&name));
    }
  }
  if (succeeded && read == VideoReadEnd) {
    StrBufAppendFormat(error, "%s: the video ended early when read a second time", path);
    succeeded = false;
  }

  VideoClose(video);
  ImageFree(&frame);
  StrBufFree(&name);
  return succeeded && read != VideoReadError;
}

// finds and prints the squats in the video at path.
static bool
AnalyseVideo(const char *path, const Options *options, StrBuf *error)
{
  Clip clip = {};
  Track track = {};
  bool succeeded = FindPoses(path, options, &clip, error);
  if (succeeded && !clip.count) {
    StrBufAppendFormat(error, "%s: the video has no frames", path);
    succeeded = false;
  }
  StrBuf trackError = {};
  bool tracked = succeeded && TrackBuild(clip.poses, clip.count, &track, &trackError);
  if (succeeded && !tracked) {
    StrBufAppendFormat(error, "%s: %s", path, StrBufText(&trackError));
  }
  StrBufFree(&trackError);
  if (succeeded && options->csvPath) {
    StrBuf csvError = {};
    if (!WriteCsv(options->csvPath, &clip, tracked ? &track : nullptr, &csvError)) {
      if (tracked) StrBufAppend(error, StrBufText(&csvError));
      tracked = false;
    }
    StrBufFree(&csvError);
  }
  if (tracked) {
    Depth depths[maxReps];
    StrBuf report = {};
    DepthCalibration calibration = DepthDefaultCalibration();
    DepthAssess(&track, &calibration, depths);
    ReportSquats(path, clip.seconds, &track, depths, options->detailed, &report);
    fputs(StrBufText(&report), stdout);
    StrBufFree(&report);
    if (options->images) tracked = WriteImages(path, &track, depths, error);
  }

  TrackFree(&track);
  ClipFree(&clip);
  return tracked;
}

// fits the calibration to the squats with known verdicts in the manifest options names,
// analysing each video in it, and prints the report.
static bool
Train(const Options *options, StrBuf *error)
{
  TrainManifest manifest = {};
  if (!TrainReadManifest(options->trainPath, &manifest, error)) {
    TrainManifestFree(&manifest);
    return false;
  }

  // a video that can't be analysed is reported and left out, rather than ending training.
  Track *tracks = MemoryResize(nullptr, manifest.videoCount, sizeof(Track));
  bool *tracked = MemoryResize(nullptr, manifest.videoCount, sizeof(bool));
  for (u64 video = 0; video < manifest.videoCount; video++) {
    const char *path = manifest.videos[video];
    fprintf(
      stderr,
      "analysing %" PRIu64 " of %" PRIu64 ": %s\n",
      video + 1,
      manifest.videoCount,
      path
    );
    Clip clip = {};
    StrBuf videoError = {};
    tracks[video] = (Track){};
    tracked[video] = CheckVideoFile(path, &videoError) &&
      FindPoses(path, options, &clip, &videoError) &&
      TrackBuild(clip.poses, clip.count, &tracks[video], &videoError);
    if (!tracked[video]) fprintf(stderr, "  left out: %s\n", StrBufText(&videoError));
    ClipFree(&clip);
    StrBufFree(&videoError);
  }

  StrBuf report = {};
  TrainResult result;
  bool fitted = TrainFit(&manifest, tracks, tracked, &result, &report);
  fputs(StrBufText(&report), stdout);
  if (!fitted) StrBufAppend(error, "none of the labelled squats could be judged");

  for (u64 video = 0; video < manifest.videoCount; video++) TrackFree(&tracks[video]);
  free(tracks);
  free(tracked);
  StrBufFree(&report);
  TrainManifestFree(&manifest);
  return fitted;
}

int
main(int argc, char **argv)
{
  MediaQuietLogging();
  Options options;
  StrBuf path = {};
  StrBuf error = {};
  if (!ParseOptions(argc, argv, &options, &error)) {
    fprintf(stderr, "%s: %s\n%s\n", programName, StrBufText(&error), usage);
    StrBufFree(&error);
    return 1;
  }

  if (options.trainPath) {
    if (!Train(&options, &error)) Fatal("%s", StrBufText(&error));
    StrBufFree(&error);
    return 0;
  }

  bool found = true;
  if (options.path) PathExpandHome(options.path, &path);
  else found = PromptForPath(&path, &error);
  if (!found || !CheckVideoFile(StrBufText(&path), &error)) Fatal("%s", StrBufText(&error));
  if (!AnalyseVideo(StrBufText(&path), &options, &error)) Fatal("%s", StrBufText(&error));

  StrBufFree(&path);
  StrBufFree(&error);
  return 0;
}
