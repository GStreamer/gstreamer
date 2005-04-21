/* GStreamer
 * Copyright (C) 2005 Stefan Kost <ensonic@users.sf.net>
 *
 * childproxy.c: test for GstChildProxy iface
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

gboolean added = FALSE;
gboolean removed = FALSE;

static void
my_child_added (GstChildProxy * parent, GstObject * child, gpointer user_data)
{
  if (child == GST_OBJECT (user_data)) {
    added = TRUE;
  }
}

static void
my_child_removed (GstChildProxy * parent, GstObject * child, gpointer user_data)
{
  if (child == GST_OBJECT (user_data)) {
    removed = TRUE;
  }
}

int
main (int argc, char *argv[])
{
  GstBin *bin;
  GstElement *child1, *child2;
  gboolean state;

  gst_init (&argc, &argv);

  if ((bin = GST_BIN (gst_bin_new ("bin"))) == NULL) {
    g_print ("Could not create a bin element!\n");
    return 1;
  }

  if ((child1 = gst_element_factory_make ("identity", "filter")) == NULL) {
    g_print ("Could not create a identity element!\n");
    return 1;
  }

  g_signal_connect (G_OBJECT (bin), "child-added", G_CALLBACK (my_child_added),
      child1);
  g_signal_connect (G_OBJECT (bin), "child-removed",
      G_CALLBACK (my_child_removed), child1);

  gst_bin_add (bin, child1);

  if (!added) {
    g_print ("ChildProxy::child-added has not been caught!\n");
    return 1;
  }

  if (gst_child_proxy_get_children_count (GST_CHILD_PROXY (bin)) != 1) {
    g_print ("ChildProxy should manage exactly one child now!\n");
    return 1;
  }

  child2 =
      GST_ELEMENT (gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (bin),
          0));
  if (child2 != child1) {
    g_print ("ChildProxy's first child is not what we have added!\n");
    return 1;
  }

  gst_child_proxy_set (GST_OBJECT (bin), "filter::silent", TRUE, NULL);

  g_object_get (G_OBJECT (child1), "silent", &state, NULL);
  if (!state) {
    g_print ("ChildProxy's child property access failed !\n");
    return 1;
  }

  gst_child_proxy_set (GST_OBJECT (bin), "filter::silent", FALSE, NULL);

  g_object_get (G_OBJECT (child1), "silent", &state, NULL);
  if (state) {
    g_print ("ChildProxy's child property access failed !\n");
    return 1;
  }

  gst_bin_remove (bin, child1);

  if (!removed) {
    g_print ("ChildProxy::child-added has not been caught!\n");
    return 1;
  }

  g_object_unref (G_OBJECT (bin));

  /* success */
  return 0;
}
