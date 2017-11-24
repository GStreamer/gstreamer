/* GStreamer unit tests for the custom printf
 * Copyright (C) 2015 Tim-Philipp Müller <tim centricular com>
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

#include <string.h>

#ifdef GST_DISABLE_GST_DEBUG
#error "Something wrong with the build system setup"
#endif

#include "gst/printf/printf.h"
/*
#include "gst/printf/printf-extension.h"
*/

static char *
test_printf (const char *format, ...)
{
  va_list varargs;
  char *str = NULL;
  int len;

  va_start (varargs, format);
  len = __gst_vasprintf (&str, format, varargs);
  va_end (varargs);

  if (len <= 0)
    return NULL;

  GST_INFO ("[%s]", str);
  return str;
}

GST_START_TEST (printf_I32_I64)
{
  guint64 v64 = 0xf1e2d3c4b5a6978f;
  guint32 v32 = 0xf1e2d3cf;
  guint vu = 0xf1e2d3cf;
  gchar *str;

  /* standard int/uint */
  str = test_printf ("x = %x", vu);
  fail_unless_equals_string (str, "x = f1e2d3cf");
  g_free (str);
  str = test_printf ("u = %u", vu);
  fail_unless_equals_string (str, "u = 4058174415");
  g_free (str);
  str = test_printf ("d = %d", vu);
  fail_unless_equals_string (str, "d = -236792881");
  g_free (str);

  /* 32 bit GLib */
  str = test_printf ("32-bit x value = %" G_GINT32_MODIFIER "x", v32);
  fail_unless_equals_string (str, "32-bit x value = f1e2d3cf");
  g_free (str);
  str = test_printf ("32-bit u value = %" G_GUINT32_FORMAT, v32);
  fail_unless_equals_string (str, "32-bit u value = 4058174415");
  g_free (str);
  str = test_printf ("32-bit d value = %" G_GINT32_FORMAT, v32);
  fail_unless_equals_string (str, "32-bit d value = -236792881");
  g_free (str);

  /* 64 bit Glib */
  str = test_printf ("64-bit x value = %" G_GINT64_MODIFIER "x", v64);
  fail_unless_equals_string (str, "64-bit x value = f1e2d3c4b5a6978f");
  g_free (str);
  str = test_printf ("64-bit u value = %" G_GUINT64_FORMAT, v64);
  fail_unless_equals_string (str, "64-bit u value = 17429726349691885455");
  g_free (str);
  str = test_printf ("64-bit d value = %" G_GINT64_FORMAT, v64);
  fail_unless_equals_string (str, "64-bit d value = -1017017724017666161");
  g_free (str);

  /* 32 bit Windows */
  str = test_printf ("I32x value = %I32x", v32);
  fail_unless_equals_string (str, "I32x value = f1e2d3cf");
  g_free (str);
  str = test_printf ("I32u value = %I32u", v32);
  fail_unless_equals_string (str, "I32u value = 4058174415");
  g_free (str);
  str = test_printf ("I32d value = %I32d", v32);
  fail_unless_equals_string (str, "I32d value = -236792881");
  g_free (str);

  /* needs testing first */
#if 0
#ifdef G_OS_WIN32
  /* 64 bit Windows */
  str = test_printf ("I64x value = %I64x", v64);
  fail_unless_equals_string (str, "I64x value = f1e2d3c4b5a6978f");
  g_free (str);
  str = test_printf ("I64u value = %I64u", v64);
  fail_unless_equals_string (str, "I64u value = 17429726349691885455");
  g_free (str);
  str = test_printf ("I64d value = %I64d", v64);
  fail_unless_equals_string (str, "I64d value = -1017017724017666161");
  g_free (str);
#endif
#endif
}

GST_END_TEST;

GST_START_TEST (printf_percent)
{
  gchar *str;

  /* standard int/uint */
  str = test_printf ("%u%%", 99);
  fail_unless_equals_string (str, "99%");
  g_free (str);
}

GST_END_TEST;

static Suite *
gst_printf_suite (void)
{
  Suite *s = suite_create ("GstPrintf");
  TCase *tc_chain = tcase_create ("gstprintf");

  tcase_set_timeout (tc_chain, 30);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, printf_I32_I64);
  tcase_add_test (tc_chain, printf_percent);

  return s;
}

GST_CHECK_MAIN (gst_printf);
