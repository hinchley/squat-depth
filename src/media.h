#pragma once

// helpers shared by the modules that call ffmpeg.
#include "strbuf.h"

// appends ffmpeg's description of the error code to error.
void
MediaAppendError(StrBuf *error, int code);

// stops ffmpeg logging anything but errors, which are reported through StrBuf instead.
void
MediaQuietLogging(void);
