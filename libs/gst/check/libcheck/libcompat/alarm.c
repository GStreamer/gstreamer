#include "libcompat.h"

unsigned int
alarm (unsigned int seconds CK_ATTRIBUTE_UNUSED)
{
  assert (0);
  return 0;
}
