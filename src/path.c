#include "path.h"

#include <stdlib.h>
#include <string.h>

void
PathExpandHome(const char *path, StrBuf *expanded)
{
  const char *home = getenv("HOME");
  if (home && path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
    StrBufAppend(expanded, home);
    StrBufAppend(expanded, path + 1);
    return;
  }
  StrBufAppend(expanded, path);
}

const char *
PathFileName(const char *path)
{
  const char *slash = strrchr(path, '/');
  return slash && slash[1] ? slash + 1 : path;
}
