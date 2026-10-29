#include "memory.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "settings.h"

void *
MemoryResize(void *pointer, u64 count, u64 size)
{
  u64 bytes;
  if (ckd_mul(&bytes, count, size)) {
    Fatal("allocation of %" PRIu64 " × %" PRIu64 " bytes overflows", count, size);
  }
  if (!bytes) bytes = 1;

  void *resized = realloc(pointer, bytes);
  if (!resized) Fatal("out of memory allocating %" PRIu64 " bytes", bytes);
  return resized;
}

void
Fatal(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  fprintf(stderr, "%s: ", programName);
  vfprintf(stderr, format, arguments);
  fputc('\n', stderr);
  va_end(arguments);
  exit(1);
}
