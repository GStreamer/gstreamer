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
#include <string.h>
#include "caps.h"

static void
check_caps (const gchar * eins, const gchar * zwei)
{
  GstCaps *one, *two, *test, *test2, *test3, *test4;

  one = gst_caps_from_string (eins);
  two = gst_caps_from_string (zwei);
  g_print ("      A  =  %u\n", strlen (eins));
  g_print ("      B  =  %u\n", strlen (zwei));

  test = gst_caps_intersect (one, two);
  if (gst_caps_is_equal (one, two)) {
    g_print ("         EQUAL\n\n");
    g_assert (gst_caps_is_equal (one, test));
    g_assert (gst_caps_is_equal (two, test));
  } else if (!gst_caps_is_any (one) || gst_caps_is_empty (two)) {
    test2 = gst_caps_subtract (one, test);
    g_print ("  A - B  =  %u\n", strlen (gst_caps_to_string (test2)));
    /* test2 = one - (one A two) = one - two */
    test3 = gst_caps_intersect (test2, two);
    g_print ("  empty  =  %s\n", gst_caps_to_string (test3));
    g_assert (gst_caps_is_empty (test3));
    gst_caps_free (test3);
    test3 = gst_caps_union (test2, two);
    g_print ("  A + B  =  %u\n", strlen (gst_caps_to_string (test3)));
    /* test3 = one - two + two = one + two */
    g_print ("  A + B  =  %s\n", gst_caps_to_string (gst_caps_subtract (one,
                test3)));
    g_assert (gst_caps_is_subset (one, test3));
    test4 = gst_caps_union (one, two);
    g_assert (gst_caps_is_equal (test3, test4));
    g_print ("         NOT EQUAL\n\n");
    gst_caps_free (test2);
    gst_caps_free (test3);
    gst_caps_free (test4);
  } else {
    g_print ("         ANY CAPS\n\n");
  }
  gst_caps_free (test);
  gst_caps_free (two);
  gst_caps_free (one);
}

gint
main (gint argc, gchar ** argv)
{
  guint i, j;

  gst_init (&argc, &argv);

  for (i = 0; i < G_N_ELEMENTS (caps_list); i++) {
    for (j = 0; j < G_N_ELEMENTS (caps_list); j++) {
      g_print ("%u - %u\n", i, j);
      check_caps (caps_list[i], caps_list[j]);
    }
  }

  return 0;
}
