#!/usr/bin/env bash
# fits the depth calibration to squats with known verdicts, listed in a manifest that TRAIN.md
# describes, and saves the report to build/train-report.txt.
#   scripts/train.sh [manifest]           reports the fit (manifest defaults to tests/videos.txt)
#   scripts/train.sh --apply [manifest]   also writes the suggested settings into
#                                         src/settings.h, then rebuilds and runs the tests
set -euo pipefail

apply=false
if [[ ${1:-} == --apply ]]; then
  apply=true
  shift
fi
manifest=$(realpath "${1:-$(dirname "$0")/../tests/videos.txt}")
cd "$(dirname "$0")/.."

./build.sh
build/squat-depth --train "$manifest" | tee build/train-report.txt
$apply || exit 0

# the report ends with the settings as they are written in settings.h, when it suggests any.
pattern='^static constexpr f64 (depthKneeTopOffset|depthModelDeviation) = [0-9.]+;$'
mapfile -t settings < <(grep -E "$pattern" build/train-report.txt || true)
if ((${#settings[@]} == 0)); then
  echo "no settings suggested, so none applied" >&2
  exit 1
fi
cp src/settings.h build/settings.h.before
for setting in "${settings[@]}"; do
  name=$(awk '{print $4}' <<<"$setting")
  sed -i "s/^static constexpr f64 $name = .*;\$/$setting/" src/settings.h
done
diff -u build/settings.h.before src/settings.h || true
./build.sh
./build.sh test
echo "applied: update the comments above them in src/settings.h to say where they came from"
