#!/usr/bin/env bash
# downloads the RTMPose-m pose model (Apache-2.0, from OpenMMLab's MMPose) into models/,
# checking the archive against its known sha256.
set -euo pipefail
cd "$(dirname "$0")/.."

url=https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmpose-m_simcc-body7_pt-body7_420e-256x192-e48f03d0_20230504.zip
archiveSha256=f7fbb6c5c11a1bb70f3d445e4ddec5d144ea89ad5649c081ef976f9b24a0b741
modelSha256=5c0a4bf67953e6d2ac43ce15e77dc9d5d354ae18430a47d2c5963a7bc5683e3c
target=models/rtmpose-m.onnx

if [[ -f $target ]] && echo "$modelSha256  $target" | sha256sum --check --quiet 2>/dev/null; then
  echo "$target is already up to date"
  exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "downloading $url"
curl --fail --location --progress-bar --output "$work/model.zip" "$url"
echo "$archiveSha256  $work/model.zip" | sha256sum --check --quiet

unzip -q -j "$work/model.zip" '*/end2end.onnx' -d "$work"
echo "$modelSha256  $work/end2end.onnx" | sha256sum --check --quiet

mkdir -p models
mv "$work/end2end.onnx" "$target"
echo "saved $target"
