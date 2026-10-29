#pragma once

// a built-in 5×7 bitmap font for labelling images: digits, letters (drawn as capitals), space,
// and + - . % °.
#include "image.h"
#include "types.h"

// a glyph's height in pixels, at scale 1. each glyph is 5 pixels wide and followed by a
// pixel of space.
static constexpr u32 fontHeight = 7;
static constexpr u32 fontAdvance = 6;

// the width in pixels of the utf-8 text drawn at scale, without the space after its last
// glyph.
u32
FontTextWidth(const char *text, u32 scale);

// draws the utf-8 text with its top left corner at x and y, each font pixel scale pixels
// square. characters the font lacks are drawn as space. parts outside the image are clipped.
void
FontDrawText(Image *image, f32 x, f32 y, const char *text, u32 scale, Color color);
