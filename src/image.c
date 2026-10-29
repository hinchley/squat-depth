#include "image.h"

#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "memory.h"

void
ImageResize(Image *image, u32 width, u32 height)
{
  u64 bytes = (u64)width * height * 3;
  if (bytes > image->capacity) {
    image->pixels = MemoryResize(image->pixels, bytes, sizeof(u8));
    image->capacity = bytes;
  }
  image->width = width;
  image->height = height;
}

void
ImageFree(Image *image)
{
  free(image->pixels);
  *image = (Image){};
}

void
ImageDrawDisc(Image *image, f32 x, f32 y, f32 radius, Color color)
{
  f32 left = fmaxf(0, floorf(x - radius));
  f32 right = fminf((f32)image->width - 1, ceilf(x + radius));
  f32 top = fmaxf(0, floorf(y - radius));
  f32 bottom = fminf((f32)image->height - 1, ceilf(y + radius));
  for (f32 pixelY = top; pixelY <= bottom; pixelY++) {
    for (f32 pixelX = left; pixelX <= right; pixelX++) {
      f32 dx = pixelX - x;
      f32 dy = pixelY - y;
      if (dx * dx + dy * dy > radius * radius) continue;
      u8 *pixel = image->pixels + (u64)pixelY * ImageStride(image) + (u64)pixelX * 3;
      pixel[0] = color.red;
      pixel[1] = color.green;
      pixel[2] = color.blue;
    }
  }
}

void
ImageDrawLine(Image *image, f32 x0, f32 y0, f32 x1, f32 y1, f32 width, Color color)
{
  // discs half a pixel apart cover the line without gaps.
  f32 length = hypotf(x1 - x0, y1 - y0);
  u64 steps = (u64)ceilf(length * 2) + 1;
  for (u64 step = 0; step <= steps; step++) {
    f32 along = (f32)step / (f32)steps;
    ImageDrawDisc(image, x0 + (x1 - x0) * along, y0 + (y1 - y0) * along, width / 2, color);
  }
}

void
ImageDrawDashedLine(
  Image *image,
  f32 x0,
  f32 y0,
  f32 x1,
  f32 y1,
  f32 width,
  f32 dash,
  Color color
)
{
  f32 length = hypotf(x1 - x0, y1 - y0);
  if (length <= 0 || dash <= 0) return;
  for (f32 start = 0; start < length; start += 2 * dash) {
    f32 from = start / length;
    f32 to = fminf(start + dash, length) / length;
    ImageDrawLine(
      image,
      x0 + (x1 - x0) * from,
      y0 + (y1 - y0) * from,
      x0 + (x1 - x0) * to,
      y0 + (y1 - y0) * to,
      width,
      color
    );
  }
}

void
ImageFillRectangle(Image *image, f32 x, f32 y, f32 width, f32 height, Color color)
{
  f32 left = fmaxf(0, roundf(x));
  f32 right = fminf((f32)image->width, roundf(x + width));
  f32 top = fmaxf(0, roundf(y));
  f32 bottom = fminf((f32)image->height, roundf(y + height));
  for (f32 pixelY = top; pixelY < bottom; pixelY++) {
    for (f32 pixelX = left; pixelX < right; pixelX++) {
      u8 *pixel = image->pixels + (u64)pixelY * ImageStride(image) + (u64)pixelX * 3;
      pixel[0] = color.red;
      pixel[1] = color.green;
      pixel[2] = color.blue;
    }
  }
}

void
ImageDrawRectangle(
  Image *image,
  f32 x,
  f32 y,
  f32 width,
  f32 height,
  f32 thickness,
  Color color
)
{
  ImageDrawLine(image, x, y, x + width, y, thickness, color);
  ImageDrawLine(image, x + width, y, x + width, y + height, thickness, color);
  ImageDrawLine(image, x + width, y + height, x, y + height, thickness, color);
  ImageDrawLine(image, x, y + height, x, y, thickness, color);
}

// encodes image as png into packet.
static bool
EncodePng(const Image *image, AVPacket *packet, StrBuf *error)
{
  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
  if (!codec) {
    StrBufAppend(error, "this ffmpeg has no png encoder");
    return false;
  }

  AVCodecContext *context = avcodec_alloc_context3(codec);
  AVFrame *frame = av_frame_alloc();
  if (!context || !frame) Fatal("out of memory starting the png encoder");
  context->width = (int)image->width;
  context->height = (int)image->height;
  context->pix_fmt = AV_PIX_FMT_RGB24;
  context->time_base = (AVRational){1, 1};
  frame->width = context->width;
  frame->height = context->height;
  frame->format = AV_PIX_FMT_RGB24;
  frame->data[0] = image->pixels;
  frame->linesize[0] = (int)ImageStride(image);

  int status = avcodec_open2(context, codec, nullptr);
  if (status >= 0) status = avcodec_send_frame(context, frame);
  if (status >= 0) status = avcodec_send_frame(context, nullptr);
  if (status >= 0) status = avcodec_receive_packet(context, packet);
  av_frame_free(&frame);
  avcodec_free_context(&context);

  if (status < 0) {
    StrBufAppend(error, "png encoding failed: ");
    MediaAppendError(error, status);
    return false;
  }
  return true;
}

bool
ImageWritePng(const Image *image, const char *path, StrBuf *error)
{
  AVPacket *packet = av_packet_alloc();
  if (!packet) Fatal("out of memory starting the png encoder");
  if (!EncodePng(image, packet, error)) {
    av_packet_free(&packet);
    return false;
  }

  FILE *file = fopen(path, "wb");
  bool written = false;
  if (file) {
    written = fwrite(packet->data, 1, (u64)packet->size, file) == (u64)packet->size;
    written = fclose(file) == 0 && written;
  }
  av_packet_free(&packet);
  if (!written) StrBufAppendFormat(error, "%s: %s", path, strerror(errno));
  return written;
}
