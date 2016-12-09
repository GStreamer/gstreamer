#include "libcompat.h"

int
timer_create (clockid_t clockid CK_ATTRIBUTE_UNUSED,
    struct sigevent *sevp CK_ATTRIBUTE_UNUSED,
    timer_t * timerid CK_ATTRIBUTE_UNUSED)
{
  /* 
   * The create function does nothing. timer_settime will use
   * alarm to set the timer, and timer_delete will stop the
   * alarm
   */

  return 0;
}
