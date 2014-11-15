#include "libcompat.h"

#if !defined(localtime_r)

struct tm *
localtime_r (const time_t * clock, struct tm *result)
{
  struct tm *now = localtime (clock);

  if (now == NULL) {
    return NULL;
  } else {
    *result = *now;
  }

  return result;
}

#endif /* !defined(localtime_r) */
