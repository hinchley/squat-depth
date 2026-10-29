#include "pose.h"

#include <errno.h>
#include <math.h>
#include <onnxruntime_c_api.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memory.h"
#include "settings.h"

static const char *const inputNames[] = {"input"};
static const char *const outputNames[] = {"simcc_x", "simcc_y"};

static const char *const keypointNames[PoseKeypointCount] = {
  "nose",
  "left_eye",
  "right_eye",
  "left_ear",
  "right_ear",
  "left_shoulder",
  "right_shoulder",
  "left_elbow",
  "right_elbow",
  "left_wrist",
  "right_wrist",
  "left_hip",
  "right_hip",
  "left_knee",
  "right_knee",
  "left_ankle",
  "right_ankle"
};

struct PoseModel {
  const OrtApi *api;
  OrtEnv *environment;
  OrtSessionOptions *options;
  OrtSession *session;
  OrtMemoryInfo *memory;

  // the cropped, normalised frame, as planes of red, green and blue.
  f32 *input;

  // where the person was in the previous frame, when following is true.
  Box box;
  bool following;
};

// true when status is success. otherwise appends what failed and onnx runtime's reason to
// error, and frees status.
static bool
Check(const OrtApi *api, OrtStatus *status, const char *what, StrBuf *error)
{
  if (!status) return true;
  // the message's first line is the reason. the rest is a stack trace.
  const char *message = api->GetErrorMessage(status);
  StrBufAppendFormat(error, "%s: %.*s", what, (int)strcspn(message, "\n"), message);
  api->ReleaseStatus(status);
  return false;
}

PoseModel *
PoseModelOpen(const char *path, StrBuf *error)
{
  if (access(path, R_OK) != 0) {
    StrBufAppendFormat(
      error,
      "cannot read the pose model %s: %s (scripts/fetch-model.sh downloads it)",
      path,
      strerror(errno)
    );
    return nullptr;
  }
  const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  if (!api) {
    StrBufAppend(error, "the installed onnx runtime is older than the one built against");
    return nullptr;
  }

  PoseModel *model = MemoryResize(nullptr, 1, sizeof(PoseModel));
  *model = (PoseModel){.api = api};
  model->input = MemoryResize(nullptr, 3ull * poseInputWidth * poseInputHeight, sizeof(f32));
  bool opened = Check(
                  api,
                  api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, programName, &model->environment),
                  "cannot start onnx runtime",
                  error
                ) &&
    Check(
      api,
      api->CreateSessionOptions(&model->options),
      "cannot start onnx runtime",
      error
    ) &&
    Check(
      api,
      api->SetSessionGraphOptimizationLevel(model->options, ORT_ENABLE_ALL),
      "cannot start onnx runtime",
      error
    ) &&
    Check(
      api,
      api->SetIntraOpNumThreads(model->options, poseThreads),
      "cannot start onnx runtime",
      error
    ) &&
    // idle threads sleep rather than spin, which otherwise keeps every core busy.
    Check(
      api,
      api->AddSessionConfigEntry(model->options, "session.intra_op.allow_spinning", "0"),
      "cannot start onnx runtime",
      error
    ) &&
    Check(
      api,
      api->CreateSession(model->environment, path, model->options, &model->session),
      "cannot load the pose model",
      error
    ) &&
    Check(
      api,
      api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &model->memory),
      "cannot start onnx runtime",
      error
    );
  if (!opened) {
    PoseModelClose(model);
    return nullptr;
  }
  return model;
}

// the frame's colour at x and y, interpolated between the four nearest pixels, with black
// beyond the edges.
static void
Sample(const Image *frame, f32 x, f32 y, f32 colour[3])
{
  f32 left = floorf(x);
  f32 top = floorf(y);
  f32 right = x - left;
  f32 below = y - top;
  colour[0] = colour[1] = colour[2] = 0;
  for (u32 corner = 0; corner < 4; corner++) {
    f32 cornerX = left + (f32)(corner & 1);
    f32 cornerY = top + (f32)(corner >> 1);
    if (cornerX < 0 || cornerY < 0) continue;
    if (cornerX >= (f32)frame->width || cornerY >= (f32)frame->height) continue;
    f32 weight = (corner & 1 ? right : 1 - right) * (corner >> 1 ? below : 1 - below);
    const u8 *pixel = frame->pixels + (u64)cornerY * ImageStride(frame) + (u64)cornerX * 3;
    for (u32 channel = 0; channel < 3; channel++) colour[channel] += weight * pixel[channel];
  }
}

// box grown by the padding the model was trained with, then made wider or taller to match
// the model's aspect ratio, about the same centre.
static Box
ModelBox(Box box)
{
  f32 aspect = (f32)poseInputWidth / (f32)poseInputHeight;
  f32 width = box.width * poseBoxPadding;
  f32 height = box.height * poseBoxPadding;
  if (width > height * aspect) height = width / aspect;
  else width = height * aspect;
  return (Box){
    .x = box.x + box.width / 2 - width / 2,
    .y = box.y + box.height / 2 - height / 2,
    .width = width,
    .height = height
  };
}

// fills the model's input with the part of frame inside box, which has the model's aspect
// ratio, scaled to the input size and normalised.
static void
Crop(PoseModel *model, const Image *frame, Box box)
{
  u64 plane = (u64)poseInputWidth * poseInputHeight;
  f32 scale = box.width / (f32)poseInputWidth;
  for (u32 y = 0; y < poseInputHeight; y++) {
    for (u32 x = 0; x < poseInputWidth; x++) {
      f32 colour[3];
      Sample(frame, box.x + (f32)x * scale, box.y + (f32)y * scale, colour);
      u64 at = (u64)y * poseInputWidth + x;
      for (u32 channel = 0; channel < 3; channel++) {
        model->input[channel * plane + at] = (colour[channel] - poseMean[channel]) /
          poseDeviation[channel];
      }
    }
  }
}

// the data of a model output, which should hold one row of bins per keypoint.
static const f32 *
OutputData(const OrtApi *api, OrtValue *output, u64 bins, StrBuf *error)
{
  OrtTensorTypeAndShapeInfo *info = nullptr;
  size_t count = 0;
  i64 shape[3] = {};
  bool read = Check(
                api,
                api->GetTensorTypeAndShape(output, &info),
                "cannot read the pose model",
                error
              ) &&
    Check(api, api->GetDimensionsCount(info, &count), "cannot read the pose model", error) &&
    count == 3 &&
    Check(api, api->GetDimensions(info, shape, 3), "cannot read the pose model", error);
  if (info) api->ReleaseTensorTypeAndShapeInfo(info);
  if (!read) {
    if (!error->length) {
      StrBufAppend(error, "the pose model's outputs have an unexpected shape");
    }
    return nullptr;
  }
  if (shape[0] != 1 || shape[1] != PoseKeypointCount || shape[2] != (i64)bins) {
    StrBufAppendFormat(
      error,
      "the pose model's outputs are %lld×%lld×%lld, not 1×%d×%llu",
      (long long)shape[0],
      (long long)shape[1],
      (long long)shape[2],
      PoseKeypointCount,
      (unsigned long long)bins
    );
    return nullptr;
  }

  void *data = nullptr;
  if (!Check(
        api,
        api->GetTensorMutableData(output, &data),
        "cannot read the pose model",
        error
      )) {
    return nullptr;
  }
  return data;
}

// the index of the largest of count values, and that value in maximum.
static u64
ArgMax(const f32 *values, u64 count, f32 *maximum)
{
  u64 best = 0;
  for (u64 index = 1; index < count; index++) {
    if (values[index] > values[best]) best = index;
  }
  *maximum = values[best];
  return best;
}

bool
PoseEstimate(PoseModel *model, const Image *frame, Box box, Pose *pose, StrBuf *error)
{
  const OrtApi *api = model->api;
  Box crop = ModelBox(box);
  Crop(model, frame, crop);

  i64 shape[4] = {1, 3, poseInputHeight, poseInputWidth};
  u64 bytes = 3ull * poseInputWidth * poseInputHeight * sizeof(f32);
  OrtValue *input = nullptr;
  OrtValue *outputs[2] = {};
  bool ran = Check(
               api,
               api->CreateTensorWithDataAsOrtValue(
                 model->memory,
                 model->input,
                 bytes,
                 shape,
                 4,
                 ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                 &input
               ),
               "cannot pass the frame to the pose model",
               error
             ) &&
    Check(
      api,
      api->Run(
        model->session,
        nullptr,
        inputNames,
        (const OrtValue *const *)&input,
        1,
        outputNames,
        2,
        outputs
      ),
      "the pose model failed",
      error
    );

  u64 binsX = (u64)((f32)poseInputWidth * poseSplitRatio);
  u64 binsY = (u64)((f32)poseInputHeight * poseSplitRatio);
  const f32 *simccX = ran ? OutputData(api, outputs[0], binsX, error) : nullptr;
  const f32 *simccY = simccX ? OutputData(api, outputs[1], binsY, error) : nullptr;
  if (simccY) {
    f32 scale = crop.width / (f32)poseInputWidth;
    for (u32 index = 0; index < PoseKeypointCount; index++) {
      f32 maximumX;
      f32 maximumY;
      u64 binX = ArgMax(simccX + index * binsX, binsX, &maximumX);
      u64 binY = ArgMax(simccY + index * binsY, binsY, &maximumY);
      pose->keypoints[index] = (Keypoint){
        .x = crop.x + (f32)binX / poseSplitRatio * scale,
        .y = crop.y + (f32)binY / poseSplitRatio * scale,
        .score = fminf(maximumX, maximumY)
      };
    }
  }

  for (u32 index = 0; index < 2; index++) {
    if (outputs[index]) api->ReleaseValue(outputs[index]);
  }
  if (input) api->ReleaseValue(input);
  return simccY != nullptr;
}

// the mean score of the hips, knees and ankles, which are what depth is judged from.
static f32
LegScore(const Pose *pose)
{
  f32 total = 0;
  for (u32 index = PoseLeftHip; index <= PoseRightAnkle; index++) {
    total += pose->keypoints[index].score;
  }
  return total / (f32)(PoseRightAnkle - PoseLeftHip + 1);
}

// the box round the pose's found keypoints, grown by the margin on each side.
static Box
KeypointBox(const Pose *pose)
{
  f32 left = INFINITY;
  f32 top = INFINITY;
  f32 right = -INFINITY;
  f32 bottom = -INFINITY;
  for (u32 index = 0; index < PoseKeypointCount; index++) {
    const Keypoint *keypoint = &pose->keypoints[index];
    if (keypoint->score < poseMinScore) continue;
    left = fminf(left, keypoint->x);
    top = fminf(top, keypoint->y);
    right = fmaxf(right, keypoint->x);
    bottom = fmaxf(bottom, keypoint->y);
  }
  f32 marginX = (right - left) * poseKeypointMargin;
  f32 marginY = (bottom - top) * poseKeypointMargin;
  return (Box){
    .x = left - marginX,
    .y = top - marginY,
    .width = right - left + 2 * marginX,
    .height = bottom - top + 2 * marginY
  };
}

bool
PoseFollow(PoseModel *model, const Image *frame, Pose *pose, Box *box, StrBuf *error)
{
  if (model->following) {
    *box = model->box;
    if (!PoseEstimate(model, frame, *box, pose, error)) return false;
    if (LegScore(pose) >= poseFollowMinScore) {
      model->box = KeypointBox(pose);
      return true;
    }
  }

  // searches the whole frame, then looks again more closely at what it found.
  *box = (Box){.width = (f32)frame->width, .height = (f32)frame->height};
  if (!PoseEstimate(model, frame, *box, pose, error)) return false;
  if (LegScore(pose) >= poseFollowMinScore) {
    *box = KeypointBox(pose);
    if (!PoseEstimate(model, frame, *box, pose, error)) return false;
  }
  model->following = LegScore(pose) >= poseFollowMinScore;
  if (model->following) model->box = KeypointBox(pose);
  return true;
}

const char *
PoseKeypointName(enum PoseKeypoint keypoint)
{
  return keypointNames[keypoint];
}

void
PoseModelClose(PoseModel *model)
{
  if (!model) return;
  const OrtApi *api = model->api;
  if (model->memory) api->ReleaseMemoryInfo(model->memory);
  if (model->session) api->ReleaseSession(model->session);
  if (model->options) api->ReleaseSessionOptions(model->options);
  if (model->environment) api->ReleaseEnv(model->environment);
  free(model->input);
  free(model);
}
