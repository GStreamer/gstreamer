/* $Id$

 * =============================================================================
 *
 * = LIBRARY
 *    pace
 *
 * = FILENAME
 *    pace/win32/mman.c
 *
 * = AUTHOR
 *    Luther Baker
 *
 * ============================================================================= */

#include "mman.h"

void *
mmap (void *addr, size_t len, int prot, int flags, HANDLE fildes, long off)
{
  void *addr_mapping = 0;
  int nt_flags = 0;
  HANDLE file_mapping = INVALID_HANDLE_VALUE;

  if (flags | MAP_PRIVATE) {
    prot = PAGE_WRITECOPY;
    nt_flags = FILE_MAP_COPY;
  } else if (flags | MAP_SHARED) {
    if (prot | PAGE_READONLY)
      nt_flags = FILE_MAP_READ;
    if (prot | PAGE_READWRITE)
      nt_flags = FILE_MAP_WRITE;
  }

  file_mapping = CreateFileMapping (fildes, 0, prot, 0, 0, 0);
  if (file_mapping == 0)
    return MAP_FAILED;

# if defined (PACE_OS_EXTRA_MMAP_FLAGS)
  nt_flags |= PACE_OS_EXTRA_MMAP_FLAGS;
# endif /* PACE_OS_EXTRA_MMAP_FLAGS */

  //ACE_UNUSED_ARG (addr);        /* WinCE does not allow specifying <addr>.*/
  addr_mapping = MapViewOfFile (file_mapping, nt_flags, 0, off, len);

  /* Only close this down if we used the temporary. */
  if (file_mapping == INVALID_HANDLE_VALUE)
    CloseHandle (file_mapping);

  if (addr_mapping == 0)
    return MAP_FAILED;

  else if ((flags | MAP_FIXED)
      && addr_mapping != addr) {
    errno = 22;
    return MAP_FAILED;
  } else
    return addr_mapping;
}

int
mprotect (void *addr, size_t len, int prot)
{
  DWORD dummy;                  /* Sigh! */

  return VirtualProtect (addr, len, prot, &dummy) ? 0 : -1;
}


int
msync (void *addr, size_t len, int flags)
{
  //PACE_UNUSED_ARG (flags);
  if (!FlushViewOfFile (addr, len))
    return -1;
  return 0;
}

int
munmap (void *addr, size_t len)
{
  //PACE_UNUSED_ARG (len);
  if (!UnmapViewOfFile (addr))
    return -1;
  return 0;

}
