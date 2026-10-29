#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

// grows the storage by half until it holds extra more bytes and the nul.
static void
Reserve(StrBuf *buffer, u64 extra)
{
  u64 needed = buffer->length + extra + 1;
  if (needed <= buffer->capacity) return;

  u64 capacity = buffer->capacity ? buffer->capacity : 16;
  while (capacity < needed) capacity += capacity / 2;
  buffer->text = MemoryResize(buffer->text, capacity, sizeof(char));
  buffer->capacity = capacity;
}

void
StrBufAppend(StrBuf *buffer, const char *text)
{
  StrBufAppendBytes(buffer, text, strlen(text));
}

void
StrBufAppendBytes(StrBuf *buffer, const char *text, u64 length)
{
  Reserve(buffer, length);
  memcpy(buffer->text + buffer->length, text, length);
  buffer->length += length;
  buffer->text[buffer->length] = '\0';
}

void
StrBufAppendFormat(StrBuf *buffer, const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  va_list copy;
  va_copy(copy, arguments);
  int length = vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  if (length < 0) Fatal("invalid format string \"%s\"", format);

  Reserve(buffer, (u64)length);
  vsnprintf(buffer->text + buffer->length, (u64)length + 1, format, arguments);
  va_end(arguments);
  buffer->length += (u64)length;
}

bool
StrBufReadLine(FILE *file, StrBuf *line)
{
  char chunk[256];
  bool read = false;
  StrBufClear(line);
  while (fgets(chunk, sizeof(chunk), file)) {
    read = true;
    u64 length = strlen(chunk);
    if (length && chunk[length - 1] == '\n') {
      StrBufAppendBytes(line, chunk, length - 1);
      return true;
    }
    StrBufAppendBytes(line, chunk, length);
  }
  return read;
}

void
StrBufClear(StrBuf *buffer)
{
  buffer->length = 0;
  if (buffer->text) buffer->text[0] = '\0';
}

void
StrBufFree(StrBuf *buffer)
{
  free(buffer->text);
  *buffer = (StrBuf){};
}
