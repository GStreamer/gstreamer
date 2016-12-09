#include "libcompat.h"

int
timer_delete (timer_t timerid CK_ATTRIBUTE_UNUSED)
{
#ifdef HAVE_SETITIMER
  /*
   * If the system does not have timer_settime() but does have
   * setitimer() use that instead of alarm().
   */
  struct itimerval interval;

  /*
   * Setting values to '0' results in disabling the running timer.
   */
  interval.it_value.tv_sec = 0;
  interval.it_value.tv_usec = 0;
  interval.it_interval.tv_sec = 0;
  interval.it_interval.tv_usec = 0;

  return setitimer (ITIMER_REAL, &interval, NULL);
#else
  /*
   * There is only one timer, that used by alarm.
   * Setting alarm(0) will not set a new alarm, and
   * will kill the previous timer.
   */

  alarm (0);

  return 0;
#endif
}
