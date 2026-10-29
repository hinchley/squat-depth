#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "image.h"
#include "media.h"
#include "strbuf.h"
#include "types.h"
#include "video.h"

static u64 failures = 0;

// records a failure, named by what, when condition is false.
static void
Expect(bool condition, const char *what)
{
  if (condition) return;
  fprintf(stderr, "image_test: failed: %s\n", what);
  failures++;
}

// writes a small png with ImageWritePng, then decodes it with VideoOpen, which ffmpeg
// reads as a one-frame video, and checks every pixel survived.
static void
TestPngRoundTrip(void)
{
  Image image = {};
  ImageResize(&image, 5, 3);
  for (u64 index = 0; index < image.capacity; index++) image.pixels[index] = (u8)(index * 17);

  char path[] = "build/image_test.png";
  StrBuf error = {};
  bool written = ImageWritePng(&image, path, &error);
  Expect(written, "ImageWritePng succeeds");
  if (!written) fprintf(stderr, "image_test: %s\n", StrBufText(&error));

  Video *video = written ? VideoOpen(path, &error) : nullptr;
  Expect(video != nullptr, "the png opens as a video");
  if (video) {
    Image decoded = {};
    f64 seconds = 0;
    VideoInfo info = VideoGetInfo(video);
    Expect(
      info.width == 5 && info.height == 3 && info.rotation == 0,
      "the png's size is kept"
    );
    Expect(VideoNextFrame(video, &decoded, &seconds, &error) == VideoReadFrame, "one frame");
    Expect(
      decoded.width == 5 && decoded.height == 3 &&
        memcmp(decoded.pixels, image.pixels, image.capacity) == 0,
      "the decoded pixels match"
    );
    Expect(VideoNextFrame(video, &decoded, &seconds, &error) == VideoReadEnd, "then the end");
    ImageFree(&decoded);
    VideoClose(video);
  }

  unlink(path);
  ImageFree(&image);
  StrBufFree(&error);
}

static void
TestOpenFailure(void)
{
  StrBuf error = {};
  Expect(!VideoOpen("build/no-such-video.mp4", &error), "a missing file does not open");
  Expect(error.length > 0, "a missing file gives a reason");
  StrBufFree(&error);
}

int
main(void)
{
  MediaQuietLogging();
  TestPngRoundTrip();
  TestOpenFailure();
  return failures ? 1 : 0;
}
