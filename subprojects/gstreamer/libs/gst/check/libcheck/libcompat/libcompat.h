/*
 * Check: a unit test framework for C
 * Copyright (C) 2001, 2002 Arien Malec
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef LIBCOMPAT_H
#define LIBCOMPAT_H

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define GCC_VERSION_AT_LEAST(major, minor) \
((__GNUC__ > (major)) || \
 (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#else
#define GCC_VERSION_AT_LEAST(major, minor) 0
#endif

#if GCC_VERSION_AT_LEAST(2,95)
#define CK_ATTRIBUTE_UNUSED __attribute__ ((unused))
#else
#define CK_ATTRIBUTE_UNUSED
#endif /* GCC 2.95 */

#if GCC_VERSION_AT_LEAST(2,5)
#define CK_ATTRIBUTE_NORETURN __attribute__ ((noreturn))
#else
#define CK_ATTRIBUTE_NORETURN
#endif /* GCC 2.5 */

/*
 * Used for MSVC to create the export attribute
 * CK_DLL_EXP is defined during the compilation of the library
 * on the command line.
 */
#ifndef CK_DLL_EXP
#define CK_DLL_EXP extern
#endif

#if _MSC_VER
#include <WinSock2.h>           /* struct timeval, API used in gettimeofday implementation */
#include <io.h>                 /* read, write */
#include <process.h>            /* getpid */
#include <BaseTsd.h>            /* for ssize_t */
typedef SSIZE_T ssize_t;
#endif /* _MSC_VER */

/* defines size_t */
#include <sys/types.h>

/* provides assert */
#include <assert.h>

/* defines FILE */
#include <stdio.h>

/* defines exit() */
#include <stdlib.h>

/* provides localtime and struct tm */
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* !HAVE_SYS_TIME_H */
#include <time.h>

/* declares fork(), _POSIX_VERSION.  according to Autoconf.info,
   unistd.h defines _POSIX_VERSION if the system is POSIX-compliant,
   so we will use this as a test for all things uniquely provided by
   POSIX like sigaction() and fork() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

/* declares pthread_create and friends */
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/* replacement functions for broken originals */
#if !HAVE_DECL_ALARM
CK_DLL_EXP unsigned int alarm (unsigned int seconds);
#endif /* !HAVE_DECL_ALARM */

#if !HAVE_MALLOC
CK_DLL_EXP void *rpl_malloc (size_t n);
#endif /* !HAVE_MALLOC */

#if !HAVE_REALLOC
CK_DLL_EXP void *rpl_realloc (void *p, size_t n);
#endif /* !HAVE_REALLOC */

#if !HAVE_GETPID && HAVE__GETPID
#define getpid _getpid
#endif /* !HAVE_GETPID && HAVE__GETPID */

#if !HAVE_GETTIMEOFDAY
CK_DLL_EXP int gettimeofday (struct timeval *tv, void *tz);
#endif /* !HAVE_GETTIMEOFDAY */

#if !HAVE_DECL_LOCALTIME_R
#if !defined(localtime_r)
CK_DLL_EXP struct tm *localtime_r (const time_t * clock, struct tm *result);
#endif
#endif /* !HAVE_DECL_LOCALTIME_R */

#if !HAVE_DECL_STRDUP && !HAVE__STRDUP
CK_DLL_EXP char *strdup (const char *str);
#elif !HAVE_DECL_STRDUP && HAVE__STRDUP
#define strdup _strdup
#endif /* !HAVE_DECL_STRDUP && HAVE__STRDUP */

#if !HAVE_DECL_STRSIGNAL
CK_DLL_EXP char *strsignal (int sig);
#endif /* !HAVE_DECL_STRSIGNAL */

/*
 * On systems where clock_gettime() is not available, or
 * on systems where some clocks may not be supported, the
 * definition for CLOCK_MONOTONIC and CLOCK_REALTIME may not
 * be available. These should define which type of clock
 * clock_gettime() should use. We define it here if it is
 * not defined simply so the reimplementation can ignore it.
 *
 * We set the values of these clocks to some (hopefully)
 * invalid value, to avoid the case where we define a
 * clock with a valid value, and unintentionally use
 * an actual good clock by accident.
 */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC -1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME -1
#endif

#ifndef HAVE_LIBRT

#ifdef STRUCT_TIMESPEC_DEFINITION_MISSING
/*
 * The following structure is defined in POSIX 1003.1 for times
 * specified in seconds and nanoseconds. If it is not defined in
 * time.g, then we need to define it here
 */
struct timespec
{
  time_t tv_sec;
  long tv_nsec;
};
#endif /* STRUCT_TIMESPEC_DEFINITION_MISSING */

#ifdef STRUCT_ITIMERSPEC_DEFINITION_MISSING
/* 
 * The following structure is defined in POSIX.1b for timer start values and intervals.
 * If it is not defined in time.h, then we need to define it here.
 */
struct itimerspec
{
  struct timespec it_interval;
  struct timespec it_value;
};
#endif /* STRUCT_ITIMERSPEC_DEFINITION_MISSING */

/* 
 * Do a simple forward declaration in case the struct is not defined.
 * In the versions of timer_create in libcompat, sigevent is never
 * used.
 */
struct sigevent;

#ifndef HAVE_CLOCK_GETTIME
CK_DLL_EXP int clock_gettime (clockid_t clk_id, struct timespec *ts);
#endif
CK_DLL_EXP int timer_create (clockid_t clockid, struct sigevent *sevp,
    timer_t * timerid);
CK_DLL_EXP int timer_settime (timer_t timerid, int flags,
    const struct itimerspec *new_value, struct itimerspec *old_value);
CK_DLL_EXP int timer_delete (timer_t timerid);
#endif /* HAVE_LIBRT */

/*
 * The following checks are to determine if the system's
 * snprintf (or its variants) should be replaced with
 * the C99 compliant version in libcompat.
 */
#if HAVE_CONFIG_H
#include <config.h>
#endif
#if HAVE_STDARG_H
#include <stdarg.h>

#if !HAVE_VSNPRINTF
CK_DLL_EXP int rpl_vsnprintf (char *, size_t, const char *, va_list);

#define vsnprintf rpl_vsnprintf
#endif
#if !HAVE_SNPRINTF
CK_DLL_EXP int rpl_snprintf (char *, size_t, const char *, ...);

#define snprintf rpl_snprintf
#endif
#endif /* HAVE_STDARG_H */

#if !HAVE_GETLINE
CK_DLL_EXP ssize_t getline (char **lineptr, size_t * n, FILE * stream);
#endif

/* silence warnings about an empty library */
CK_DLL_EXP void
ck_do_nothing (void)
    CK_ATTRIBUTE_NORETURN;

#endif /* !LIBCOMPAT_H */
