/* GStreamer
 * Copyright (C) 2009 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#include <stdlib.h>
#include <gst/gst.h>
#include <gst/interfaces/propertyprobe.h>

int
main (int argc, char *argv[])
{
  GstElement *src, *sink;
  GstElement *bin;
  GstPropertyProbe *probe = NULL;
  const GParamSpec *pspec = NULL;
  GValueArray *array = NULL;
  gint i, ret;
  GValue *value;
  const gchar *device;
  gchar *name;
  guint flags;

  gst_init (&argc, &argv);

  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  src = gst_element_factory_make ("v4lsrc", "v4l_source");
  g_assert (src);
  sink = gst_element_factory_make ("fakesink", "fake_sink");
  g_assert (sink);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), src, sink, NULL);
  /* link the elements */
  gst_element_link_many (src, sink, NULL);

  /* probe devices */
  g_print ("Probing devices with propertyprobe...\n");
  probe = GST_PROPERTY_PROBE (src);
  pspec = gst_property_probe_get_property (probe, "device");
  array = gst_property_probe_probe_and_get_values (probe, pspec);

  if (!array) {
    g_print ("No device found\n");
    exit (1);
  }

  for (i = 0; i < array->n_values; i++) {
    value = g_value_array_get_nth (array, i);
    device = g_value_get_string (value);
    g_print ("Device: %s\n", device);
    g_object_set_property (G_OBJECT (src), "device", value);
    gst_element_set_state (bin, GST_STATE_READY);
    ret = gst_element_get_state (bin, NULL, NULL, 10 * GST_SECOND);
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      g_print ("Couldn't set STATE_READY\n");
      continue;
    }
    g_object_get (G_OBJECT (src), "device-name", &name, NULL);
    g_print ("Name: %s\n", name);
    g_free (name);
    g_object_get (G_OBJECT (src), "flags", &flags, NULL);
    g_print ("Flags: 0x%08X\n", flags);
    gst_element_set_state (bin, GST_STATE_NULL);
    g_print ("\n");
  }

  exit (0);
}
