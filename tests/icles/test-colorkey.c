/* GStreamer
 * Copyright (C) <2008> Stefan Kost <ensonic@users.sf.net>
 *
 * test-colorkey: test manual colorkey handling
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <gst/interfaces/xoverlay.h>

static GtkWidget *video_window = NULL;
static GstElement *sink = NULL;
static guint embed_xid = 0;
static GdkGC *trans_gc = NULL;

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  if ((GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT) &&
      gst_structure_has_name (message->structure, "prepare-xwindow-id")) {
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));

    g_print ("got prepare-xwindow-id\n");
    if (!embed_xid) {
      embed_xid = GDK_WINDOW_XID (GDK_WINDOW (video_window->window));
    }

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (element),
            "force-aspect-ratio")) {
      g_object_set (element, "force-aspect-ratio", TRUE, NULL);
    }

    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (GST_MESSAGE_SRC (message)),
        embed_xid);
  }
  return GST_BUS_PASS;
}

static gboolean
handle_resize_cb (GtkWidget * widget, GdkEventConfigure * event, gpointer data)
{
  gdk_draw_rectangle (widget->window, widget->style->white_gc, TRUE,
      0, 0, widget->allocation.width, widget->allocation.height);

  if (trans_gc) {
    guint x, y;
    guint h = widget->allocation.height * 0.75;

    gdk_draw_rectangle (widget->window, trans_gc, TRUE,
        0, 0, widget->allocation.width, h);

    for (y = h; y < widget->allocation.height; y++) {
      for (x = 0; x < widget->allocation.width; x++) {
        if (((x & 1) || (y & 1)) && (x & 1) != (y & 1)) {
          gdk_draw_point (widget->window, trans_gc, x, y);
        }
      }
    }
  }
  return FALSE;
}

static void
msg_state_changed (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  const GstStructure *s;

  s = gst_message_get_structure (message);

  /* We only care about state changed on the pipeline */
  if (s && GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
    GstState old, new, pending;
    gint color;

    gst_message_parse_state_changed (message, &old, &new, &pending);

    /* When state of the pipeline changes to paused or playing we start updating scale */
    switch (GST_STATE_TRANSITION (old, new)) {
      case GST_STATE_CHANGE_READY_TO_PAUSED:
        g_object_get (G_OBJECT (sink), "colorkey", &color, NULL);
        if (color != -1) {
          GdkColor trans_color = { 0,
            (color & 0xff0000) >> 8,
            (color & 0xff00),
            (color & 0xff) << 8
          };

          trans_gc = gdk_gc_new (video_window->window);
          gdk_gc_set_rgb_fg_color (trans_gc, &trans_color);
        }
        handle_resize_cb (video_window, NULL, NULL);
        break;
      default:
        break;
    }
  }
}

static void
window_closed (GtkWidget * widget, GdkEvent * event, gpointer user_data)
{
  GstElement *pipeline = user_data;

  g_print ("stopping\n");
  gtk_widget_hide_all (widget);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gtk_main_quit ();
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GstElement *pipeline, *src;
  GstBus *bus;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  if (!XInitThreads ()) {
    g_print ("XInitThreads failed\n");
    exit (-1);
  }

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  /* prepare the pipeline */

  pipeline = gst_pipeline_new ("xvoverlay");
  src = gst_element_factory_make ("videotestsrc", NULL);
  sink = gst_element_factory_make ("xvimagesink", NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  g_object_set (G_OBJECT (sink),
      "autopaint-colorkey", FALSE,
      "force-aspect-ratio", TRUE, "draw-borders", FALSE, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler,
      pipeline);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (msg_state_changed), pipeline);
  gst_object_unref (bus);

  /* prepare the ui */

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  video_window = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (video_window, FALSE);

  gtk_window_set_default_size (GTK_WINDOW (window), 320, 240);
  gtk_container_add (GTK_CONTAINER (window), video_window);
  g_signal_connect (G_OBJECT (window), "delete-event",
      G_CALLBACK (window_closed), (gpointer) pipeline);
  g_signal_connect (G_OBJECT (video_window), "configure-event",
      G_CALLBACK (handle_resize_cb), NULL);

  /* show the gui. */
  gtk_widget_show_all (window);

  //connect_bus_signals (pipeline);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_main ();
  gst_object_unref (pipeline);

  return 0;
}
