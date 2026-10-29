#include <stdio.h>

#include "font.h"
#include "image.h"
#include "types.h"

static u64 failures = 0;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "font_test: failed: %s\n", what);
  failures++;
}

// whether the pixel at x and y is color.
static bool
IsColor(const Image *image, u32 x, u32 y, Color color)
{
  const u8 *pixel = image->pixels + (u64)y * ImageStride(image) + (u64)x * 3;
  return pixel[0] == color.red && pixel[1] == color.green && pixel[2] == color.blue;
}

// the count of pixels in the image that are color.
static u64
CountColor(const Image *image, Color color)
{
  u64 count = 0;
  for (u32 y = 0; y < image->height; y++) {
    for (u32 x = 0; x < image->width; x++) count += IsColor(image, x, y, color);
  }
  return count;
}

// a black image of width by height.
static Image
Blank(u32 width, u32 height)
{
  Image image = {};
  ImageResize(&image, width, height);
  for (u64 index = 0; index < (u64)width * height * 3; index++) image.pixels[index] = 0;
  return image;
}

static constexpr Color white = {255, 255, 255};

static void
TestWidth(void)
{
  Expect(FontTextWidth("", 2) == 0, "empty text has no width");
  Expect(FontTextWidth("A", 1) == 5, "one glyph is 5 wide");
  Expect(FontTextWidth("AB", 2) == 22, "glyphs are spaced and scaled");
  Expect(FontTextWidth("1°", 1) == 11, "a two-byte character is one glyph");
  Expect(FontTextWidth("→x", 1) == 11, "a three-byte character is one glyph");
}

static void
TestDraw(void)
{
  Image image = Blank(20, 20);
  FontDrawText(&image, 1, 1, "-", 1, white);
  // the minus is the middle row of the glyph, all 5 pixels across.
  Expect(CountColor(&image, white) == 5, "a minus is 5 pixels");
  Expect(IsColor(&image, 1, 4, white) && IsColor(&image, 5, 4, white), "where it should be");
  ImageFree(&image);

  // an I is 3 pixels across at the top and bottom and 1 down the middle, each 2×2 at scale 2.
  image = Blank(20, 20);
  FontDrawText(&image, 0, 0, "i", 2, white);
  Expect(CountColor(&image, white) == 4 * (3 + 5 + 3), "lower case is drawn as capitals");
  ImageFree(&image);

  image = Blank(20, 20);
  FontDrawText(&image, 15, 16, "8", 3, white);
  Expect(CountColor(&image, white) > 0, "text off the edge is clipped, not skipped");
  ImageFree(&image);
}

static void
TestShapes(void)
{
  Image image = Blank(10, 10);
  ImageFillRectangle(&image, -3, 8, 5, 5, white);
  Expect(CountColor(&image, white) == 2 * 2, "a rectangle is clipped to the image");

  Image line = Blank(100, 5);
  ImageDrawDashedLine(&line, 0, 2, 100, 2, 1, 10, white);
  Expect(IsColor(&line, 5, 2, white), "a dash is drawn");
  Expect(!IsColor(&line, 15, 2, white), "a gap is left");
  Expect(IsColor(&line, 25, 2, white), "the next dash is drawn");
  ImageFree(&image);
  ImageFree(&line);
}

int
main(void)
{
  TestWidth();
  TestDraw();
  TestShapes();
  return failures ? 1 : 0;
}
