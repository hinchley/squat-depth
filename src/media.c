#include "media.h"

#include <libavutil/error.h>
#include <libavutil/log.h>

void
MediaAppendError(StrBuf *error, int code)
{
  char description[AV_ERROR_MAX_STRING_SIZE];
  if (av_strerror(code, description, sizeof(description)) != 0) {
    StrBufAppendFormat(error, "ffmpeg error %d", code);
    return;
  }
  StrBufAppend(error, description);
}

void
MediaQuietLogging(void)
{
  av_log_set_level(AV_LOG_FATAL);
}
