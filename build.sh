#!/usr/bin/env bash
# builds squat-depth with gcc.
#   ./build.sh          optimised build, to build/squat-depth
#   ./build.sh debug    unoptimised build with address and undefined-behaviour sanitisers
#   ./build.sh test     formatting check, then builds and runs every tests/*_test.c
#   ./build.sh format   formats src/ and tests/ in place with clang-format
set -euo pipefail
cd "$(dirname "$0")"

mode=${1:-release}

# pkg-config packages linked into every build.
packages=(libavformat libavcodec libavutil libswscale libonnxruntime)

warnings=(
  -Wall -Wextra -Wpedantic -Werror
  -Wconversion -Wsign-conversion -Wshadow
  -Wformat=2 -Wformat-nonliteral
  -Wjump-misses-init -Wstrict-prototypes -Wmissing-prototypes
  -Wvla -Wnull-dereference
)

cflags=(-std=c23 "${warnings[@]}")
libs=(-lm)
if ((${#packages[@]})); then
  read -ra packageFlags <<<"$(pkg-config --cflags "${packages[@]}")"
  read -ra packageLibs <<<"$(pkg-config --libs "${packages[@]}")"
  # library headers are system headers, so the warnings above apply only to our code.
  cflags+=("${packageFlags[@]/#-I/-isystem}")
  libs=("${packageLibs[@]}" "${libs[@]}")
fi

formatted=(src/*.c src/*.h)
shopt -s nullglob
tests=(tests/*_test.c)
shopt -u nullglob
formatted+=("${tests[@]}")

mkdir -p build

case $mode in
  release)
    gcc "${cflags[@]}" -O2 -g src/*.c -o build/squat-depth "${libs[@]}"
    ;;
  debug)
    gcc "${cflags[@]}" -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
      src/*.c -o build/squat-depth "${libs[@]}"
    ;;
  test)
    clang-format --dry-run --Werror "${formatted[@]}"
    # STYLE.md: a body that does not fit on the line of its if, else or loop gets braces.
    if grep -nE '^\s*(if|else if|for|while)\b.*\)$|^\s*else$' "${formatted[@]}"; then
      echo "braceless body on the next line (see STYLE.md)" >&2
      exit 1
    fi
    library=()
    for source in src/*.c; do
      [[ $source == src/main.c ]] || library+=("$source")
    done
    for test in "${tests[@]}"; do
      name=$(basename "$test" .c)
      gcc "${cflags[@]}" -O0 -g -fsanitize=address,undefined -Isrc \
        "$test" "${library[@]}" -o "build/$name" "${libs[@]}"
      "build/$name"
    done
    echo "all ${#tests[@]} test programs passed"
    ;;
  format)
    clang-format -i "${formatted[@]}"
    ;;
  *)
    echo "usage: $0 [release|debug|test|format]" >&2
    exit 1
    ;;
esac
