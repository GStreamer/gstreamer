#include <windows.h>

//VC7 or later, building with pre-VC7 runtime libraries
    long
_ftol2 (double dblSource)
{
  return _ftol (dblSource);
}


