
#ifndef __RESAMPLE_DEBUG_H__
#define __RESAMPLE_DEBUG_H__

#if 0
enum
{
  RESAMPLE_LEVEL_NONE = 0,
  RESAMPLE_LEVEL_ERROR,
  RESAMPLE_LEVEL_WARNING,
  RESAMPLE_LEVEL_INFO,
  RESAMPLE_LEVEL_DEBUG,
  RESAMPLE_LEVEL_LOG
};

#define RESAMPLE_ERROR(...) \
  RESAMPLE_DEBUG_LEVEL(RESAMPLE_LEVEL_ERROR, __VA_ARGS__)
#define RESAMPLE_WARNING(...) \
  RESAMPLE_DEBUG_LEVEL(RESAMPLE_LEVEL_WARNING, __VA_ARGS__)
#define RESAMPLE_INFO(...) \
  RESAMPLE_DEBUG_LEVEL(RESAMPLE_LEVEL_INFO, __VA_ARGS__)
#define RESAMPLE_DEBUG(...) \
  RESAMPLE_DEBUG_LEVEL(RESAMPLE_LEVEL_DEBUG, __VA_ARGS__)
#define RESAMPLE_LOG(...) \
  RESAMPLE_DEBUG_LEVEL(RESAMPLE_LEVEL_LOG, __VA_ARGS__)

#define RESAMPLE_DEBUG_LEVEL(level,...) \
  resample_debug_log ((level), __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

void resample_debug_log (int level, const char *file, const char *function,
    int line, const char *format, ...);
void resample_debug_set_level (int level);
int resample_debug_get_level (void);
#else

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (libaudioresample_debug);
#define GST_CAT_DEFAULT libaudioresample_debug

#define RESAMPLE_ERROR GST_ERROR
#define RESAMPLE_WARNING GST_WARNING
#define RESAMPLE_INFO GST_INFO
#define RESAMPLE_DEBUG GST_DEBUG
#define RESAMPLE_LOG GST_LOG

#define resample_debug_set_level(x) do { } while (0)

#endif

#endif
