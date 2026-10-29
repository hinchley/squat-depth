#include "video.h"

#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "memory.h"

struct Video {
  AVFormatContext *format;
  AVCodecContext *decoder;
  AVPacket *packet;
  AVFrame *decoded;
  SwsContext *scaler;
  int streamIndex;
  u32 rotation;

  // the stream's time of its first frame, in seconds.
  f64 startTime;
  f64 timeBase;
  f64 frameRate;

  // the count of frames returned so far.
  u64 frames;

  // the converted frame before it is rotated, when there is a rotation.
  Image unrotated;
};

// the clockwise rotation, as 0, 90, 180 or 270, that shows the stream upright.
static u32
ReadRotation(const AVStream *stream)
{
  const AVPacketSideData *data = av_packet_side_data_get(
    stream->codecpar->coded_side_data,
    stream->codecpar->nb_coded_side_data,
    AV_PKT_DATA_DISPLAYMATRIX
  );
  if (!data || data->size < 9 * sizeof(i32)) return 0;

  // ffmpeg gives the angle the matrix turns the frame counterclockwise.
  f64 counterClockwise = av_display_rotation_get((const i32 *)data->data);
  if (isnan(counterClockwise)) return 0;
  i64 clockwise = (i64)lround(-counterClockwise);
  clockwise = ((clockwise % 360) + 360) % 360;
  return (u32)(((clockwise + 45) / 90 * 90) % 360);
}

// appends "path: " and ffmpeg's description of status to error.
static void
AppendFailure(StrBuf *error, const char *path, const char *what, int status)
{
  StrBufAppendFormat(error, "%s: %s: ", path, what);
  MediaAppendError(error, status);
}

Video *
VideoOpen(const char *path, StrBuf *error)
{
  Video *video = MemoryResize(nullptr, 1, sizeof(Video));
  *video = (Video){};

  int status = avformat_open_input(&video->format, path, nullptr, nullptr);
  if (status < 0) {
    AppendFailure(error, path, "cannot open", status);
    VideoClose(video);
    return nullptr;
  }
  status = avformat_find_stream_info(video->format, nullptr);
  if (status < 0) {
    AppendFailure(error, path, "cannot read the stream details", status);
    VideoClose(video);
    return nullptr;
  }

  const AVCodec *codec = nullptr;
  status = av_find_best_stream(video->format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (status < 0) {
    AppendFailure(error, path, "no usable video stream", status);
    VideoClose(video);
    return nullptr;
  }
  video->streamIndex = status;
  const AVStream *stream = video->format->streams[status];

  video->decoder = avcodec_alloc_context3(codec);
  video->packet = av_packet_alloc();
  video->decoded = av_frame_alloc();
  if (!video->decoder || !video->packet || !video->decoded) {
    Fatal("out of memory starting the video decoder");
  }
  status = avcodec_parameters_to_context(video->decoder, stream->codecpar);
  // zero lets the decoder pick a thread count.
  video->decoder->thread_count = 0;
  if (status >= 0) status = avcodec_open2(video->decoder, codec, nullptr);
  if (status < 0) {
    AppendFailure(error, path, "cannot start the video decoder", status);
    VideoClose(video);
    return nullptr;
  }

  video->rotation = ReadRotation(stream);
  video->timeBase = av_q2d(stream->time_base);
  if (stream->start_time != AV_NOPTS_VALUE) {
    video->startTime = (f64)stream->start_time * video->timeBase;
  }
  if (stream->avg_frame_rate.num && stream->avg_frame_rate.den) {
    video->frameRate = av_q2d(stream->avg_frame_rate);
  }
  return video;
}

VideoInfo
VideoGetInfo(const Video *video)
{
  const AVCodecParameters *parameters = video->format->streams[video->streamIndex]->codecpar;
  bool sideways = video->rotation == 90 || video->rotation == 270;
  u32 width = (u32)parameters->width;
  u32 height = (u32)parameters->height;
  VideoInfo info = {
    .width = sideways ? height : width,
    .height = sideways ? width : height,
    .rotation = video->rotation,
    .frameRate = video->frameRate
  };
  if (video->format->duration != AV_NOPTS_VALUE) {
    info.duration = (f64)video->format->duration / AV_TIME_BASE;
  }
  return info;
}

// sends the next packet of the video stream to the decoder, or the end-of-stream flush when
// the file has no more.
static bool
FeedDecoder(Video *video, StrBuf *error)
{
  while (true) {
    int status = av_read_frame(video->format, video->packet);
    if (status == AVERROR_EOF) {
      status = avcodec_send_packet(video->decoder, nullptr);
      if (status >= 0 || status == AVERROR_EOF) return true;
      StrBufAppend(error, "cannot flush the video decoder: ");
      MediaAppendError(error, status);
      return false;
    }
    if (status < 0) {
      StrBufAppend(error, "cannot read the video: ");
      MediaAppendError(error, status);
      return false;
    }
    if (video->packet->stream_index != video->streamIndex) {
      av_packet_unref(video->packet);
      continue;
    }

    status = avcodec_send_packet(video->decoder, video->packet);
    av_packet_unref(video->packet);
    if (status >= 0 || status == AVERROR(EAGAIN)) return true;
    StrBufAppend(error, "cannot decode the video: ");
    MediaAppendError(error, status);
    return false;
  }
}

// copies source into target turned clockwise by rotation, which is 90, 180 or 270.
static void
Rotate(const Image *source, u32 rotation, Image *target)
{
  bool sideways = rotation == 90 || rotation == 270;
  ImageResize(
    target,
    sideways ? source->height : source->width,
    sideways ? source->width : source->height
  );

  u64 stride = ImageStride(target);
  for (u32 y = 0; y < source->height; y++) {
    const u8 *from = source->pixels + y * ImageStride(source);
    for (u32 x = 0; x < source->width; x++, from += 3) {
      u32 targetX = rotation == 90 ? source->height - 1 - y
        : rotation == 180          ? source->width - 1 - x
                                   : y;
      u32 targetY = rotation == 90 ? x
        : rotation == 180          ? source->height - 1 - y
                                   : source->width - 1 - x;
      memcpy(target->pixels + targetY * stride + (u64)targetX * 3, from, 3);
    }
  }
}

// converts the decoded frame to upright rgb in frame.
static bool
Convert(Video *video, Image *frame, StrBuf *error)
{
  AVFrame *decoded = video->decoded;
  video->scaler = sws_getCachedContext(
    video->scaler,
    decoded->width,
    decoded->height,
    (enum AVPixelFormat)decoded->format,
    decoded->width,
    decoded->height,
    AV_PIX_FMT_RGB24,
    SWS_BILINEAR,
    nullptr,
    nullptr,
    nullptr
  );
  if (!video->scaler) {
    StrBufAppend(error, "cannot convert the video's pixel format to rgb");
    return false;
  }

  Image *converted = video->rotation ? &video->unrotated : frame;
  ImageResize(converted, (u32)decoded->width, (u32)decoded->height);
  u8 *planes[4] = {converted->pixels};
  int strides[4] = {(int)ImageStride(converted)};
  sws_scale(
    video->scaler,
    (const u8 *const *)decoded->data,
    decoded->linesize,
    0,
    decoded->height,
    planes,
    strides
  );
  if (video->rotation) Rotate(&video->unrotated, video->rotation, frame);
  return true;
}

enum VideoRead
VideoNextFrame(Video *video, Image *frame, f64 *seconds, StrBuf *error)
{
  while (true) {
    int status = avcodec_receive_frame(video->decoder, video->decoded);
    if (status == AVERROR_EOF) return VideoReadEnd;
    if (status == AVERROR(EAGAIN)) {
      if (!FeedDecoder(video, error)) return VideoReadError;
      continue;
    }
    if (status < 0) {
      StrBufAppend(error, "cannot decode the video: ");
      MediaAppendError(error, status);
      return VideoReadError;
    }

    i64 timestamp = video->decoded->best_effort_timestamp;
    if (timestamp != AV_NOPTS_VALUE) {
      *seconds = (f64)timestamp * video->timeBase - video->startTime;
    } else {
      *seconds = video->frameRate ? (f64)video->frames / video->frameRate : 0;
    }
    bool converted = Convert(video, frame, error);
    av_frame_unref(video->decoded);
    if (!converted) return VideoReadError;
    video->frames++;
    return VideoReadFrame;
  }
}

void
VideoClose(Video *video)
{
  if (!video) return;
  sws_freeContext(video->scaler);
  av_frame_free(&video->decoded);
  av_packet_free(&video->packet);
  avcodec_free_context(&video->decoder);
  avformat_close_input(&video->format);
  ImageFree(&video->unrotated);
  free(video);
}
