/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/gst.h>

void
assert_on_error (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line, GObject * object,
    GstDebugMessage * message, gpointer data)
{
  g_assert (level != GST_LEVEL_ERROR);
}

gint
main (gint argc, gchar * argv[])
{
  /* this file contains random tests for stuff that went wrong in some version
   * and should be tested so we're sure it works right now 
   * Please add what exactly the code tests for in your test */

  gst_init (&argc, &argv);

  /* TEST 1:
   * gstcaps.c 1.120 used a code path that caused a GST_ERROR for the tested 
   * caps when simplifying even though that is absolutely valid */
  {
    GstCaps *caps =
        gst_caps_from_string
        ("some/type, a=(int)2, b=(int)3, c=bla; some/type, a=(int)2, c=bla");
    gst_debug_add_log_function (assert_on_error, NULL);
    gst_caps_do_simplify (caps);
    gst_debug_remove_log_function (assert_on_error);
    gst_caps_free (caps);
  }

  /* TEST 2:
   * gstvalue.c 1.34 had a broken comparison function for int ranges that 
   * returned GST_VALUE_EQUAL even though the range end was different */
  {
    GValue v1 = { 0, };
    GValue v2 = { 0, };

    g_value_init (&v1, GST_TYPE_INT_RANGE);
    g_value_init (&v2, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (&v1, 1, 2);
    gst_value_set_int_range (&v2, 1, 3);
    g_assert (gst_value_compare (&v1, &v2) != GST_VALUE_EQUAL);
    g_value_unset (&v1);
    g_value_unset (&v2);
  }

  return 0;
}
