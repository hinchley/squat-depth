#include "report.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "font.h"
#include "path.h"

static constexpr Color passColor = {40, 210, 70};
static constexpr Color failColor = {235, 55, 45};
static constexpr Color thighColor = {255, 255, 255};
static constexpr Color kneeTopColor = {255, 210, 0};
static constexpr Color outlineColor = {0, 0, 0};

// the label's font scale, and its padding inside its box and the box's from the frame's edge.
static constexpr u32 labelScale = 2;
static constexpr f32 labelPadding = 4;
static constexpr f32 labelMargin = 6;

u32
ReportConfidence(f64 probability)
{
  f64 percent = floor(fmax(probability, 1 - probability) * 100);
  return (u32)fmin(fmax(percent, 50), 99);
}

void
ReportSquats(
  const char *path,
  const f64 *seconds,
  const Track *track,
  const Depth *depths,
  bool detailed,
  StrBuf *report
)
{
  StrBufAppendFormat(
    report,
    "%s: %" PRIu64 " squat%s\n",
    PathFileName(path),
    track->repCount,
    track->repCount == 1 ? "" : "s"
  );
  if (detailed) {
    StrBufAppendFormat(
      report,
      "  %" PRIu64 " frames, %s side facing the camera, thigh %.0f pixels\n",
      track->count,
      track->side == TrackSideLeft ? "left" : "right",
      track->thighLength
    );
  }
  if (!track->repCount) {
    StrBufAppend(report, "  the hip never dropped far enough below standing for a squat\n");
    return;
  }

  for (u64 index = 0; index < track->repCount; index++) {
    const Rep *rep = &track->reps[index];
    const Depth *depth = &depths[index];
    StrBufAppendFormat(
      report,
      "  squat %" PRIu64 "  bottom %.2f s  thigh %+.1f°  margin %+.2f  %s  %" PRIu32
      "%% confident this %s\n",
      index + 1,
      seconds[rep->bottom],
      depth->thighAngle,
      depth->margin,
      depth->passed ? "PASS" : "FAIL",
      ReportConfidence(depth->probability),
      depth->passed ? "was a good lift" : "was not to depth"
    );
    if (!rep->complete) {
      StrBufAppend(report, "    the video ends before the lifter stands up again\n");
    }
    if (!detailed) continue;
    StrBufAppendFormat(
      report,
      "    %.2f–%.2f s, bottom frame %" PRIu64 ", margin %+.3f ± %.3f (model %.3f, jitter "
      "%.3f, score %.3f), p %.3f\n",
      seconds[rep->start],
      seconds[rep->end],
      rep->bottom,
      depth->margin,
      depth->deviation,
      depth->modelDeviation,
      depth->jitterDeviation,
      depth->scoreDeviation,
      depth->probability
    );
  }
}

void
ReportDrawBottom(Image *frame, const Track *track, const Depth *depth)
{
  // lines and markers are sized to the thigh, so they look the same at any distance.
  f64 length = track->thighLength;
  f32 width = (f32)fmax(2, length / 30);
  f32 radius = (f32)fmax(3, length / 16);
  const Keypoint *hip = &track->hips[depth->bottom];
  const Keypoint *knee = &track->knees[depth->bottom];
  const DepthGeometry *geometry = &depth->geometry;
  Color verdictColor = depth->passed ? passColor : failColor;

  f32 left = (f32)(fmin(hip->x, knee->x) - length / 2);
  f32 right = (f32)(fmax(hip->x, knee->x) + length / 2);
  f32 kneeTopY = (f32)geometry->kneeTopY;
  ImageDrawDashedLine(frame, left, kneeTopY, right, kneeTopY, width, 3 * width, kneeTopColor);
  ImageDrawLine(frame, hip->x, hip->y, knee->x, knee->y, width, thighColor);
  f32 creaseX = (f32)geometry->creaseX;
  f32 creaseY = (f32)geometry->creaseY;
  ImageDrawDisc(frame, creaseX, creaseY, radius + 1, outlineColor);
  ImageDrawDisc(frame, creaseX, creaseY, radius, verdictColor);

  StrBuf label = {};
  StrBufAppendFormat(
    &label,
    "%+.1f° %s %" PRIu32 "%%",
    depth->thighAngle,
    depth->passed ? "PASS" : "FAIL",
    ReportConfidence(depth->probability)
  );
  f32 textWidth = (f32)FontTextWidth(StrBufText(&label), labelScale);
  f32 textHeight = (f32)(fontHeight * labelScale);
  ImageFillRectangle(
    frame,
    labelMargin,
    labelMargin,
    textWidth + 2 * labelPadding,
    textHeight + 2 * labelPadding,
    outlineColor
  );
  FontDrawText(
    frame,
    labelMargin + labelPadding,
    labelMargin + labelPadding,
    StrBufText(&label),
    labelScale,
    verdictColor
  );
  StrBufFree(&label);
}
