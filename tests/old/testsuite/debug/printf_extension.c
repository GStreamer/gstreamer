/*
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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

gint
main (gint argc, gchar * argv[])
{
  GstCaps *caps;
  GstElement *element;
  int zero = 0;

  gst_init (&argc, &argv);

  caps = gst_caps_from_string ("audio/x-raw-int, rate=44100");

  element = gst_element_factory_make ("identity", NULL);

  GST_ERROR ("This should print caps: %" GST_PTR_FORMAT, caps);
  GST_ERROR ("This should print an object: %" GST_PTR_FORMAT, element);
  GST_ERROR ("This should print null: %" GST_PTR_FORMAT, NULL);
  GST_ERROR ("This should print a pointer: %" GST_PTR_FORMAT, &zero);
  //GST_ERROR ("This should print a pointer: %" GST_PTR_FORMAT, (void *)1);

  return 0;
}
