/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans at gmail.com>
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

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>

#if 0
#include <gst/interfaces/propertyprobe.h>

static void
test_element (const gchar * name)
{
  GstElement *element;
  GstPropertyProbe *probe = NULL;
  const GParamSpec *pspec = NULL;
  GValueArray *array = NULL;
  guint i;

  g_print ("testing element %s\n", name);
  element = gst_element_factory_make (name, NULL);
  g_assert (element);

  gst_element_set_state (element, GST_STATE_READY);
  probe = GST_PROPERTY_PROBE (element);
  pspec = gst_property_probe_get_property (probe, "device");

  array = gst_property_probe_probe_and_get_values (probe, pspec);
  g_assert (array);

  for (i = 0; i < array->n_values; i++) {
    GValue *device = NULL;
    gchar *name = NULL;

    device = g_value_array_get_nth (array, i);
    g_object_set_property (G_OBJECT (element), "device", device);
    g_object_get (G_OBJECT (element), "device-name", &name, NULL);

    g_print ("device: %s (%s)\n", g_value_get_string (device),
        GST_STR_NULL (name));
  }
  g_value_array_free (array);

  gst_element_set_state (element, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (element));
}
#endif

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

#if 0
  test_element ("pulsesink");
  test_element ("pulsesrc");
#endif

  return 0;
}
