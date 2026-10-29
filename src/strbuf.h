#pragma once

// a growable, nul-terminated string, used for error messages and report text.
#include "types.h"
#include <stdio.h>

// zero it with {} to start empty.
// text is nullptr until the first append, so use StrBufText to read it.
typedef struct StrBuf {
  char *text;
  u64 length;
  u64 capacity;
} StrBuf;

// appends the nul-terminated text.
void
StrBufAppend(StrBuf *buffer, const char *text);

// appends length bytes of text.
void
StrBufAppendBytes(StrBuf *buffer, const char *text, u64 length);

// appends the formatted text.
[[gnu::format(printf, 2, 3)]] void
StrBufAppendFormat(StrBuf *buffer, const char *format, ...);

// reads one line from file into line, replacing its text, without the newline.
// false at end of file when nothing was read.
bool
StrBufReadLine(FILE *file, StrBuf *line);

// empties the buffer and keeps its storage.
void
StrBufClear(StrBuf *buffer);

// frees the storage and leaves the buffer empty.
void
StrBufFree(StrBuf *buffer);

// the text, or "" when nothing has been appended.
static inline const char *
StrBufText(const StrBuf *buffer)
{
  return buffer->text ? buffer->text : "";
}
