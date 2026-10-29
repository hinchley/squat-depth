#include <stdio.h>
#include <string.h>

#include "strbuf.h"
#include "types.h"

static u64 failures = 0;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "strbuf_test: failed: %s\n", what);
  failures++;
}

static void
TestEmpty(void)
{
  StrBuf buffer = {};
  Expect(strcmp(StrBufText(&buffer), "") == 0, "empty buffer reads as \"\"");
  StrBufClear(&buffer);
  Expect(buffer.length == 0, "clearing an empty buffer leaves it empty");
  StrBufFree(&buffer);
}

static void
TestAppend(void)
{
  StrBuf buffer = {};
  StrBufAppend(&buffer, "hip ");
  StrBufAppendBytes(&buffer, "kneecap", 4);
  StrBufAppendFormat(&buffer, " %d%%", 95);
  Expect(strcmp(StrBufText(&buffer), "hip knee 95%") == 0, "appends concatenate");
  Expect(buffer.length == 12, "length counts the appended bytes");

  StrBufClear(&buffer);
  StrBufAppend(&buffer, "x");
  Expect(strcmp(StrBufText(&buffer), "x") == 0, "clear empties the text");
  StrBufFree(&buffer);
  Expect(!buffer.text && !buffer.length && !buffer.capacity, "free zeroes the buffer");
}

static void
TestGrowth(void)
{
  StrBuf buffer = {};
  for (u64 index = 0; index < 10'000; index++) StrBufAppend(&buffer, "ab");
  Expect(buffer.length == 20'000, "long appends keep every byte");
  Expect(buffer.capacity > buffer.length, "capacity leaves room for the nul");
  Expect(buffer.text[19'998] == 'a' && buffer.text[20'000] == '\0', "long text is intact");
  StrBufFree(&buffer);
}

int
main(void)
{
  TestEmpty();
  TestAppend();
  TestGrowth();
  return failures ? 1 : 0;
}
