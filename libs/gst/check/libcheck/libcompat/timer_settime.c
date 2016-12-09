#include "libcompat.h"

int
timer_settime (timer_t timerid CK_ATTRIBUTE_UNUSED,
    int flags CK_ATTRIBUTE_UNUSED,
    const struct itimerspec *new_value,
    struct itimerspec *old_value CK_ATTRIBUTE_UNUSED)
{
#ifdef HAVE_SETITIMER
  /*
   * If the system does not have timer_settime() but does have
   * setitimer() use that instead of alarm().
   */
  struct itimerval interval;

  interval.it_value.tv_sec = new_value->it_value.tv_sec;
  interval.it_value.tv_usec = new_value->it_value.tv_nsec / 1000;
  interval.it_interval.tv_sec = new_value->it_interval.tv_sec;
  interval.it_interval.tv_usec = new_value->it_interval.tv_nsec / 1000;

  return setitimer (ITIMER_REAL, &interval, NULL);
#else
  int seconds = new_value->it_value.tv_sec;

  /* 
   * As the alarm() call has only second precision, if the caller
   * specifies partial seconds, we round up to the nearest second.
   */
  if (new_value->it_value.tv_nsec > 0) {
    seconds += 1;
  }

  alarm (seconds);

  return 0;
#endif
}
