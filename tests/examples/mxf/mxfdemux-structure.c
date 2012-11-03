/* GStreamer
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gtk/gtk.h>

static GtkWidget *window = NULL;
static GtkTreeStore *treestore = NULL;

static gchar *
g_value_to_string (const GValue * val)
{
  if (G_VALUE_TYPE (val) == GST_TYPE_BUFFER) {
    GstBuffer *buf = gst_value_get_buffer (val);
    GstMapInfo map;
    gchar *ret;

    gst_buffer_map (buf, &map, GST_MAP_READ);
    ret = g_base64_encode (map.data, map.size);
    gst_buffer_unmap (buf, &map);

    return ret;
  } else {
    GValue s = { 0, };
    gchar *ret;

    g_value_init (&s, G_TYPE_STRING);

    if (!g_value_transform (val, &s)) {
      return NULL;
    }

    ret = g_value_dup_string (&s);
    g_value_unset (&s);

    return ret;
  }
}

static gboolean
insert_field (GQuark field_id, const GValue * val, gpointer user_data)
{
  GtkTreeIter *parent_iter = user_data;
  GtkTreeIter iter;
  const gchar *f = g_quark_to_string (field_id);

  gtk_tree_store_append (treestore, &iter, parent_iter);

  if (G_VALUE_TYPE (val) == GST_TYPE_ARRAY) {
    guint n = gst_value_array_get_size (val);
    guint i;
    GtkTreeIter child_iter;

    gtk_tree_store_set (treestore, &iter, 0, f, -1);

    for (i = 0; i < n; i++) {
      const GValue *ve = gst_value_array_get_value (val, i);

      gtk_tree_store_append (treestore, &child_iter, &iter);

      if (G_VALUE_TYPE (ve) == GST_TYPE_STRUCTURE) {
        const GstStructure *s = gst_value_get_structure (ve);

        gtk_tree_store_set (treestore, &child_iter, 0,
            gst_structure_get_name (s), -1);

        gst_structure_foreach (s, insert_field, &child_iter);
      } else {
        gchar *v = g_value_to_string (ve);

        gtk_tree_store_set (treestore, &child_iter, 0, v, -1);

        g_free (v);
      }
    }
  } else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE) {
    const GstStructure *s = gst_value_get_structure (val);
    gchar *entry = g_strdup_printf ("%s: %s", f, gst_structure_get_name (s));

    gtk_tree_store_set (treestore, &iter, 0, entry, -1);

    g_free (entry);

    gst_structure_foreach (s, insert_field, &iter);
  } else {
    gchar *v = g_value_to_string (val);
    gchar *entry = g_strdup_printf ("%s: %s", f, v);

    gtk_tree_store_set (treestore, &iter, 0, entry, -1);

    g_free (v);
    g_free (entry);
  }

  return TRUE;
}

static void
insert_structure (const GstStructure * s, GtkTreeIter * iter)
{
  const gchar *name = gst_structure_get_name (s);

  gtk_tree_store_set (treestore, iter, 0, name, -1);

  gst_structure_foreach (s, insert_field, iter);
}

static void
on_message (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_WARNING:
    case GST_MESSAGE_ERROR:
      g_error ("Got error");
      gtk_main_quit ();
      break;
    case GST_MESSAGE_TAG:{
      GstTagList *tags;
      GValue v = { 0, };

      g_print ("Got tags\n");
      gst_message_parse_tag (message, &tags);

      if (gst_tag_list_copy_value (&v, tags, "mxf-structure")) {
        const GstStructure *s;
        GtkTreeIter iter;

        s = gst_value_get_structure (&v);

        gtk_tree_store_append (treestore, &iter, NULL);
        insert_structure (s, &iter);

        gtk_widget_show_all (window);

        g_value_unset (&v);
      }

      gst_tag_list_unref (tags);
      break;
    }
    default:
      break;
  }
}

static void
on_pad_added (GstElement * src, GstPad * pad, gpointer data)
{
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  GstPad *sinkpad = gst_element_get_static_pad (fakesink, "sink");
  GstElement *bin = (GstElement *) gst_element_get_parent (src);

  gst_bin_add (GST_BIN (bin), fakesink);

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
  gst_object_unref (bin);
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *mxfdemux;
  GstBus *bus;
  GtkWidget *scrolled_window, *treeview;

  if (argc < 2) {
    g_print ("usage: %s MXF-FILE\n", argv[0]);
    return -1;
  }

  gst_init (NULL, NULL);
  gtk_init (NULL, NULL);

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", argv[1], NULL);

  mxfdemux = gst_element_factory_make ("mxfdemux", "mxfdemux");
  g_signal_connect (mxfdemux, "pad-added", G_CALLBACK (on_pad_added), NULL);

  if (!src || !mxfdemux) {
    g_error ("Unable to create all elements");
    return -2;
  }

  gst_bin_add_many (GST_BIN (pipeline), src, mxfdemux, NULL);
  if (!gst_element_link_many (src, mxfdemux, NULL)) {
    g_error ("Failed to link elements");
    return -3;
  }

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (on_message), NULL);
  gst_object_unref (bus);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
  g_signal_connect (window, "delete-event", gtk_main_quit, NULL);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);

  treestore = gtk_tree_store_new (1, G_TYPE_STRING, NULL);
  treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (treestore));

  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview),
      gtk_tree_view_column_new_with_attributes ("Element",
          gtk_cell_renderer_text_new (), "text", 0, NULL));

  gtk_container_add (GTK_CONTAINER (scrolled_window), treeview);
  gtk_container_add (GTK_CONTAINER (window), scrolled_window);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gtk_main ();

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
