/*
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gst/gst.h>

static void
check_caps (const gchar * set, const gchar * subset)
{
  GstCaps *one, *two, *test, *test2;

  g_print ("        A  =  %s\n", set);
  one = gst_caps_from_string (set);
  g_print ("        B  =  %s\n", subset);
  two = gst_caps_from_string (subset);
  /* basics */
  test = gst_caps_subtract (one, one);
  g_assert (gst_caps_is_empty (test));
  gst_caps_free (test);
  test = gst_caps_subtract (two, two);
  g_assert (gst_caps_is_empty (test));
  gst_caps_free (test);
  test = gst_caps_subtract (two, one);
  g_assert (gst_caps_is_empty (test));
  gst_caps_free (test);
  /* now the nice part */
  test = gst_caps_subtract (one, two);
  g_assert (!gst_caps_is_empty (test));
  g_print ("    A - B  =  %s\n", gst_caps_to_string (test));
  test2 = gst_caps_union (test, two);
  g_print ("A - B + B  =  %s\n", gst_caps_to_string (test2));
  gst_caps_free (test);
  test = gst_caps_subtract (test2, one);
  g_assert (gst_caps_is_empty (test));
  gst_caps_free (test);
}

gint
main (gint argc, gchar ** argv)
{
  gst_init (&argc, &argv);

  check_caps ("some/mime, _int = [ 1, 2 ], list = { \"A\", \"B\", \"C\" }",
      "some/mime, _int = 1, list = \"A\"");
  check_caps ("some/mime, _double = (double) 1.0; other/mime, _int = { 1, 2 }",
      "some/mime, _double = (double) 1.0");

  return 0;
}
