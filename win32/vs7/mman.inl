/* $Id$ -*- C -*-

 * =============================================================================
 *
 * = LIBRARY
 *    pace
 *
 * = FILENAME
 *    pace/win32/mman.inl
 *
 * = AUTHOR
 *    Luther Baker
 *
 * ============================================================================= */

#include <io.h>

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_mlock (const void * addr, pace_size_t len)
{
  PACE_UNUSED_ARG (addr);
  PACE_UNUSED_ARG (len);
  PACE_ERRNO_NO_SUPPORT_RETURN (-1);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_mlockall (int flags)
{
  PACE_UNUSED_ARG (flags);
  PACE_ERRNO_NO_SUPPORT_RETURN (-1);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
void *
pace_mmap (void * addr,
           size_t len,
           int prot,
           int flags,
           PACE_HANDLE fildes,
           pace_off_t off)
{
  return mmap (addr, len, prot, flags, fildes, off);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_munlock (const void * addr, size_t len)
{
  PACE_UNUSED_ARG (addr);
  PACE_UNUSED_ARG (len);
  PACE_ERRNO_NO_SUPPORT_RETURN (-1);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_mprotect (void * addr, size_t len, int prot)
{
  return mprotect (addr, len, prot);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_msync (void * addr,
            size_t len,
            int flags)
{
  return msync (addr, len, flags);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_munlockall ()
{
  PACE_ERRNO_NO_SUPPORT_RETURN (-1);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_munmap (void * addr, size_t len)
{
  return munmap (addr, len);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
PACE_HANDLE
pace_shm_open (const char * name, int oflag, pace_mode_t mode)
{
  /* Would be similar to ACE_OS::open
     which (currently uses threads and Object Manager).
   */
  PACE_HANDLE retval = PACE_INVALID_HANDLE;
  PACE_UNUSED_ARG (name);
  PACE_UNUSED_ARG (oflag);
  PACE_UNUSED_ARG (mode);
  PACE_ERRNO_NO_SUPPORT_RETURN (retval);
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */

#if (PACE_HAS_POSIX_NONUOF_FUNCS)
PACE_INLINE
int
pace_shm_unlink (const char * name)
{
#if defined (__BORLANDC__)
  return unlink (name);
#else /* __BORLANDC__ */
  return _unlink (name);
#endif /* __BORLANDC__ */
}
#endif /* PACE_HAS_POSIX_NONUOF_FUNCS */
