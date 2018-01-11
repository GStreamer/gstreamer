/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstabi.c: Unit test for ABI compatibility
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/check/gstcheck.h>

#ifdef HAVE_CPU_I386
#ifndef G_OS_WIN32
#include "struct_i386.h"
#else
#include "struct_i386w.h"
#endif
#define HAVE_ABI_SIZES TRUE
#else
#ifdef __powerpc64__
#include "struct_ppc64.h"
#define HAVE_ABI_SIZES FALSE
#else
#ifdef __powerpc__
#include "struct_ppc32.h"
#define HAVE_ABI_SIZES TRUE
#else
#ifdef HAVE_CPU_X86_64
#ifndef G_OS_WIN32
#include "struct_x86_64.h"
#else
#include "struct_x86_64w.h"
#endif
#define HAVE_ABI_SIZES TRUE
#else
#ifdef HAVE_CPU_HPPA
#include "struct_hppa.h"
#define HAVE_ABI_SIZES FALSE
#else
#ifdef HAVE_CPU_SPARC
#include "struct_sparc.h"
#define HAVE_ABI_SIZES FALSE
#else
#ifdef HAVE_CPU_ARM
#include "struct_arm.h"
#define HAVE_ABI_SIZES TRUE
#else
/* in case someone wants to generate a new arch */
#include "struct_i386.h"
#define HAVE_ABI_SIZES FALSE
#endif
#endif
#endif
#endif
#endif
#endif
#endif

GST_START_TEST (test_ABI)
{
  gst_check_abi_list (list, HAVE_ABI_SIZES);
}

GST_END_TEST;

static Suite *
gstabi_suite (void)
{
  Suite *s = suite_create ("GstABI");
  TCase *tc_chain = tcase_create ("size check");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ABI);
  return s;
}

GST_CHECK_MAIN (gstabi);
