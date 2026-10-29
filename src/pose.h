#pragma once

// estimates a person's 2D pose in video frames with the RTMPose model, run by onnx runtime.
#include "image.h"
#include "strbuf.h"
#include "types.h"

// the COCO keypoints, in the model's order.
enum PoseKeypoint {
  PoseNose,
  PoseLeftEye,
  PoseRightEye,
  PoseLeftEar,
  PoseRightEar,
  PoseLeftShoulder,
  PoseRightShoulder,
  PoseLeftElbow,
  PoseRightElbow,
  PoseLeftWrist,
  PoseRightWrist,
  PoseLeftHip,
  PoseRightHip,
  PoseLeftKnee,
  PoseRightKnee,
  PoseLeftAnkle,
  PoseRightAnkle,
  PoseKeypointCount
};

// a keypoint's position in frame pixels, and the model's confidence in it.
// scores are not probabilities: about 0.3 and above is a usable keypoint.
typedef struct Keypoint {
  f32 x;
  f32 y;
  f32 score;
} Keypoint;

typedef struct Pose {
  Keypoint keypoints[PoseKeypointCount];
} Pose;

// a rectangle in frame pixels.
typedef struct Box {
  f32 x;
  f32 y;
  f32 width;
  f32 height;
} Box;

typedef struct PoseModel PoseModel;

// the model loaded from the onnx file at path, or nullptr with the reason appended to error.
PoseModel *
PoseModelOpen(const char *path, StrBuf *error);

// estimates the pose of the person inside box in frame.
bool
PoseEstimate(PoseModel *model, const Image *frame, Box box, Pose *pose, StrBuf *error);

// estimates the pose of the person in the next frame of a video, looking where the person
// was in the previous frame, or in the whole frame at the start or when the person is lost.
// box is set to where it looked.
bool
PoseFollow(PoseModel *model, const Image *frame, Pose *pose, Box *box, StrBuf *error);

// the name of the keypoint, such as "left_hip".
const char *
PoseKeypointName(enum PoseKeypoint keypoint);

// frees the model.
void
PoseModelClose(PoseModel *model);
