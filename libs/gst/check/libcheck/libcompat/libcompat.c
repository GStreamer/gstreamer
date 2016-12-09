#include "libcompat.h"

/* silence warnings about an empty library */
void
ck_do_nothing (void)
{
  assert (0);

  /*
   * to silence warning about this function actually
   * returning, but being marked as noreturn. assert()
   * must be marked as a function that returns.
   */
  exit (1);
}
