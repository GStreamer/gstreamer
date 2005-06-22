/*
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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


gint
main (gint argc, gchar ** argv)
{
  guint i;
  guint ret = 0;
  GstCaps *caps;

  gst_init (&argc, &argv);

  for (i = 0; i < G_N_ELEMENTS (caps_list); i++) {
    g_print ("getting caps from string %s\n", caps_list[i]);
    caps = gst_caps_from_string (caps_list[i]);
    if (!caps) {
      ++ret;
      g_print ("Could not get caps from string %s\n", caps_list[i]);
    } else {
      g_free (caps);
    }
  }

  return ret;
}
