#pragma once

// file path helpers.
#include "strbuf.h"

// appends path to expanded, with a leading ~ replaced by $HOME.
void
PathExpandHome(const char *path, StrBuf *expanded);

// the part of path after its last /.
const char *
PathFileName(const char *path);
