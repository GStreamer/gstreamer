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

/* statistics junkie!!! */
static guint size_before = 0, size_after = 0;
static guint length_before = 0, length_after = 0;
static guint impossible = 0, success = 0, failure = 0;

static void
check_caps (GstCaps * caps)
{
  gchar *before, *after;
  GstCaps *old;

  before = gst_caps_to_string (caps);
  old = gst_caps_copy (caps);
  gst_caps_do_simplify (caps);
  after = gst_caps_to_string (caps);
  g_assert (gst_caps_get_size (caps) <= gst_caps_get_size (old));
  if (gst_caps_get_size (caps) == gst_caps_get_size (old))
    g_assert (strlen (after) <= strlen (before));
  g_assert (gst_caps_is_equal (caps, old));
  g_print ("%s %2u/%-4u => %2u/%-4u\n",
      gst_caps_get_size (caps) < gst_caps_get_size (old) ||
      strlen (after) < strlen (before) ? "REDUCED" :
      (gst_caps_get_size (old) < 2 ? "  ---  " : "       "),
      gst_caps_get_size (old), strlen (before),
      gst_caps_get_size (caps), strlen (after));

  size_before += gst_caps_get_size (old);
  size_after += gst_caps_get_size (caps);
  length_before += strlen (before);
  length_after += strlen (after);
  if (gst_caps_get_size (old) < 2) {
    impossible++;
  } else if (gst_caps_get_size (caps) < gst_caps_get_size (old) ||
      strlen (after) < strlen (before)) {
    success++;
  } else {
    failure++;
  }

  g_free (before);
  g_free (after);
  gst_caps_free (old);
}

gint
main (gint argc, gchar ** argv)
{
  guint i, j;

  gst_init (&argc, &argv);

  for (i = 0; i < G_N_ELEMENTS (caps_list); i++) {
    GstCaps *caps = gst_caps_from_string (caps_list[i]);

    g_print ("     %2u ", i);
    check_caps (caps);
    if (!gst_caps_is_any (caps)) {
      for (j = 0; j < G_N_ELEMENTS (caps_list); j++) {
        GstCaps *caps2 = gst_caps_from_string (caps_list[j]);

        /* subtraction */
        GstCaps *temp = gst_caps_subtract (caps, caps2);

        g_print ("%2u - %2u ", i, j);
        check_caps (temp);
        gst_caps_free (temp);
        /* union */
        temp = gst_caps_union (caps, caps2);
        g_print ("%2u + %2u ", i, j);
        check_caps (temp);
        if (i == j)
          g_assert (gst_caps_get_size (caps) == gst_caps_get_size (temp));
        gst_caps_free (temp);
        gst_caps_free (caps2);
      }
    }
    gst_caps_free (caps);
  }
  g_print ("\n\nSTATISTICS:\n");
  g_print ("\nOf all caps tried\n");
  g_print ("%3u (%02.4g%%) caps were already at minimum size.\n", impossible,
      100.0 * ((double) impossible) / (impossible + success + failure));
  g_print ("%3u (%02.4g%%) caps were successfully reduced.\n", success,
      100.0 * ((double) success) / (impossible + success + failure));
  g_print ("%3u (%02.4g%%) caps could not be reduced.\n", failure,
      100.0 * ((double) failure) / (impossible + success + failure));
  g_print ("\nOf all caps that could possibly be reduced\n");
  g_print ("%02.4g%% were reduced\n",
      100.0 * ((double) success) / (success + failure));
  g_print ("%02.4g%% average reduction in caps structure amount\n",
      100.0 * (1.0 - ((double) size_after) / size_before));
  g_print ("%02.4g%% average reduction in caps serialization length\n",
      100.0 * (1.0 - ((double) length_after) / length_before));

  return 0;
}
