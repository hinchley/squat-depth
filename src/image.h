#pragma once

// rgb images: storage, and writing them out as png files.
#include "strbuf.h"
#include "types.h"

// an 8-bit rgb image, stored row by row with no padding, so a row is width * 3 bytes.
// zero it with {} to start empty, and free it with ImageFree.
typedef struct Image {
  u8 *pixels;
  u32 width;
  u32 height;
  u64 capacity;
} Image;

typedef struct Color {
  u8 red;
  u8 green;
  u8 blue;
} Color;

// sets the image's size, keeping its storage when it is big enough.
// the pixels are left undefined.
void
ImageResize(Image *image, u32 width, u32 height);

// frees the pixels and leaves the image empty.
void
ImageFree(Image *image);

// draws a filled disc centred on x and y. parts outside the image are clipped.
void
ImageDrawDisc(Image *image, f32 x, f32 y, f32 radius, Color color);

// draws a line from x0, y0 to x1, y1, width pixels thick, with round ends.
void
ImageDrawLine(Image *image, f32 x0, f32 y0, f32 x1, f32 y1, f32 width, Color color);

// draws a dashed line from x0, y0 to x1, y1, width pixels thick, in dashes dash pixels long
// with gaps as long between them.
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
);

// fills a rectangle with its top left corner at x and y. parts outside the image are clipped.
void
ImageFillRectangle(Image *image, f32 x, f32 y, f32 width, f32 height, Color color);

// draws the outline of a rectangle, thickness pixels thick.
void
ImageDrawRectangle(
  Image *image,
  f32 x,
  f32 y,
  f32 width,
  f32 height,
  f32 thickness,
  Color color
);

// writes the image to path as a png file.
bool
ImageWritePng(const Image *image, const char *path, StrBuf *error);

// the bytes in one row of the image.
static inline u64
ImageStride(const Image *image)
{
  return (u64)image->width * 3;
}
