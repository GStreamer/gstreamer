/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>

#if HAVE_CPU_X86_64

typedef struct xmm_reg
{
  guint64 a, b;
} xmm_reg;
typedef gint64 x86_reg;
#define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v
#define DECLARE_ASM_CONST(n,t,v)    static const t __attribute__((used)) __attribute__ ((aligned (n))) v

#if defined(__APPLE__)
#    define EXTERN_PREFIX "_"
#else
#    define EXTERN_PREFIX ""
#endif

#if defined(__PIC__)
#    define LOCAL_MANGLE(a) #a "(%%rip)"
#else
#    define LOCAL_MANGLE(a) #a
#endif

#define MANGLE(a) EXTERN_PREFIX LOCAL_MANGLE(a)

DECLARE_ASM_CONST (16, xmm_reg, pb_1) = {
0x0101010101010101ULL, 0x0101010101010101ULL};

DECLARE_ASM_CONST (16, xmm_reg, pw_1) = {
0x0001000100010001ULL, 0x0001000100010001ULL};


#define HAVE_SSE2_INLINE 1

#if HAVE_SSSE3_INLINE
#define COMPILE_TEMPLATE_SSE2 1
#define COMPILE_TEMPLATE_SSSE3 1
#undef RENAME
#define RENAME(a) a ## _ssse3
#include "yadif_template.c"
#undef COMPILE_TEMPLATE_SSSE3
#endif

#if HAVE_SSE2_INLINE
#undef RENAME
#define RENAME(a) a ## _sse2
#include "yadif_template.c"
#undef COMPILE_TEMPLATE_SSE2
#endif

#if HAVE_MMXEXT_INLINE
#undef RENAME
#define RENAME(a) a ## _mmxext
#include "yadif_template.c"
#endif


void filter_line_x86_64 (guint8 * dst,
    guint8 * prev, guint8 * cur, guint8 * next,
    int w, int prefs, int mrefs, int parity, int mode);

void
filter_line_x86_64 (guint8 * dst,
    guint8 * prev, guint8 * cur, guint8 * next,
    int w, int prefs, int mrefs, int parity, int mode)
{
#if 0
#if HAVE_MMXEXT_INLINE
  if (cpu_flags & AV_CPU_FLAG_MMXEXT)
    yadif->filter_line = yadif_filter_line_mmxext;
#endif
#if HAVE_SSE2_INLINE
  if (cpu_flags & AV_CPU_FLAG_SSE2)
    yadif->filter_line = yadif_filter_line_sse2;
#endif
#if HAVE_SSSE3_INLINE
  if (cpu_flags & AV_CPU_FLAG_SSSE3)
    yadif->filter_line = yadif_filter_line_ssse3;
#endif
#endif
  yadif_filter_line_sse2 (dst, prev, cur, next, w, prefs, mrefs, parity, mode);
}

#endif
