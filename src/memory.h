#pragma once

// allocation, and the exit for errors the program cannot recover from.
#include "types.h"

// pointer resized to hold count items of size bytes, or a new block when pointer is nullptr.
// a failed allocation, or a count and size whose product overflows, ends in Fatal.
void *
MemoryResize(void *pointer, u64 count, u64 size);

// prints the formatted message to stderr, prefixed with the program name, and exits with 1.
[[noreturn, gnu::format(printf, 1, 2)]] void
Fatal(const char *format, ...);
