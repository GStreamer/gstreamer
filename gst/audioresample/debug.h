
#ifndef __RESAMPLE_DEBUG_H__
#define __RESAMPLE_DEBUG_H__

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

#endif
