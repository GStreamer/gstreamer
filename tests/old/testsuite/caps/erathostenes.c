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
#include <stdlib.h>

#define MAX_SIEVE 20

static void
erathostenes (GValue * sieve, gboolean up, int size)
{
  guint i, j;
  GValue temp = { 0, };
  GValue list = { 0, };

  g_value_init (sieve, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (sieve, 2, size * size);
  for (i = up ? 2 : size; up ? (i <= size) : (i >= 2); i += up ? 1 : -1) {
    g_value_init (&list, GST_TYPE_LIST);
    for (j = 2 * i; j <= size * size; j += i) {
      GValue v = { 0, };

      g_value_init (&v, G_TYPE_INT);
      g_value_set_int (&v, j);
      gst_value_list_append_value (&list, &v);
      g_value_unset (&v);
    }
    gst_value_subtract (&temp, sieve, &list);
    g_value_unset (sieve);
    gst_value_init_and_copy (sieve, &temp);
    g_value_unset (&temp);
    g_value_unset (&list);
    /* g_print ("%2u:  %s\n", i, gst_value_serialize (sieve)); */
  }

  g_print ("%s\n", gst_value_serialize (sieve));
}

gint
main (gint argc, gchar ** argv)
{
  GValue up = { 0, };
  GValue down = { 0, };
  guint size = MAX_SIEVE;

  gst_init (&argc, &argv);

  if (argc > 1)
    size = atol (argv[1]);

  erathostenes (&up, TRUE, size);
  erathostenes (&down, FALSE, size);

  g_assert (gst_value_compare (&up, &down) == GST_VALUE_EQUAL);
  return 0;
}
