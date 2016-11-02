#include "libcompat.h"

#ifdef __APPLE__
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>
#endif

#define NANOSECONDS_PER_SECOND 1000000000



int
clock_gettime (clockid_t clk_id CK_ATTRIBUTE_UNUSED, struct timespec *ts)
{

#ifdef __APPLE__
  /* OS X does not have clock_gettime, use mach_absolute_time */

  static mach_timebase_info_data_t sTimebaseInfo;
  uint64_t rawTime;
  uint64_t nanos;

  rawTime = mach_absolute_time ();

  /*
   * OS X has a function to convert abs time to nano seconds: AbsoluteToNanoseconds
   * However, the function may not be available as we may not have
   * access to CoreServices. Because of this, we convert the abs time
   * to nano seconds manually.
   */

  /*
   * First grab the time base used on the system, if this is the first
   * time we are being called. We can check if the value is uninitialized,
   * as the denominator will be zero. 
   */
  if (sTimebaseInfo.denom == 0) {
    (void) mach_timebase_info (&sTimebaseInfo);
  }

  /* 
   * Do the conversion. We hope that the multiplication doesn't 
   * overflow; the price you pay for working in fixed point.
   */
  nanos = rawTime * sTimebaseInfo.numer / sTimebaseInfo.denom;

  /* 
   * Fill in the timespec container 
   */
  ts->tv_sec = nanos / NANOSECONDS_PER_SECOND;
  ts->tv_nsec = nanos - (ts->tv_sec * NANOSECONDS_PER_SECOND);
#else
  /* 
   * As there is no function to fall back onto to get the current
   * time, zero out the time so the caller will have a sane value. 
   */
  ts->tv_sec = 0;
  ts->tv_nsec = 0;
#endif

  return 0;
}
