/*
    $Id$

    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __MJPEG_TYPES_H__
#define __MJPEG_TYPES_H__
//#include <config.h>

#if defined(HAVE_STDINT_H)
# include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
# include <inttypes.h>
#elif defined(__CYGWIN__)
# include <sys/types.h>
typedef u_int8_t uint8_t;
typedef u_int16_t uint16_t;
typedef u_int32_t uint32_t;
typedef u_int64_t uint64_t;
# define INT8_C(c)     c
# define INT16_C(c)    c
# define INT32_C(c)    c
# define INT64_C(c)    c ## LL
# define UINT8_C(c)    c ## U
# define UINT16_C(c)   c ## U
# define UINT32_C(c)   c ## U
# define UINT64_C(c)   c ## ULL
#else
/* warning ISO/IEC 9899:1999 <stdint.h> was missing and even <inttypes.h> */
/* fixme */
/* (Ronald) we'll just give an error now...Better solutions might come later */
#error You don't seem to have sys/types.h, inttypes.h or stdint.h! \
This might mean two things: \
Either you really don't have them, in which case you should \
install the system headers and/or C-library headers. \
You might also have forgotten to define whether you have them. \
You can do this by either defining their presence before including \
mjpegtools' header files (e.g. "#define HAVE_STDINT_H"), or you can check \
for their presence in a configure script. mjpegtools' configure \
script is a good example of how to do this. You need to check for \
PRId64, stdbool.h, inttypes.h, stdint.h and sys/types.h
#endif /* HAVE_STDINT_H */

#if defined(__FreeBSD__)
#include <sys/types.h> /* FreeBSD - ssize_t */
#endif

#if defined(HAVE_STDBOOL_H) && !defined(__cplusplus)
#include <stdbool.h>
#else
/* ISO/IEC 9899:1999 <stdbool.h> missing -- enabling workaround */

# ifndef __cplusplus
typedef enum
  {
    false = 0,
    true = 1
  } locBool;

#  define false   false
#  define true    true
#  define bool locBool
# endif
#endif

#ifndef PRId64
#define PRId64 PRID64_STRING_FORMAT
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define GNUC_PRINTF( format_idx, arg_idx )    \
  __attribute__((format (printf, format_idx, arg_idx)))
#define GNUC_SCANF( format_idx, arg_idx )     \
  __attribute__((format (scanf, format_idx, arg_idx)))
#define GNUC_FORMAT( arg_idx )                \
  __attribute__((format_arg (arg_idx)))
#define GNUC_NORETURN                         \
  __attribute__((noreturn))
#define GNUC_CONST                            \
  __attribute__((const))
#define GNUC_UNUSED                           \
  __attribute__((unused))
#define GNUC_PACKED                           \
  __attribute__((packed))
#else   /* !__GNUC__ */
#define GNUC_PRINTF( format_idx, arg_idx )
#define GNUC_SCANF( format_idx, arg_idx )
#define GNUC_FORMAT( arg_idx )
#define GNUC_NORETURN
#define GNUC_CONST
#define GNUC_UNUSED
#define GNUC_PACKED
#endif  /* !__GNUC__ */


#endif /* __MJPEG_TYPES_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
