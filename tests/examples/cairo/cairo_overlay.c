/* GStreamer
 * Copyright (C) 2011 Jon Nordby <jononor@gmail.com>
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

/*
 * Example showing usage of the cairooverlay element
 * 
 * Note: The example program not run on non-X11 platforms because
 * it is using the xvimageoverlay element. That part of the code was
 * roughly based on gst_x_overlay documentation.
 */


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/interfaces/xoverlay.h>

#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-gobject.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

static gulong video_window_xid = 0;

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;
  if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  if (video_window_xid != 0) {
    GstXOverlay *xoverlay;

    xoverlay = GST_X_OVERLAY (GST_MESSAGE_SRC (message));
    gst_x_overlay_set_window_handle (xoverlay, video_window_xid);
  } else {
    g_warning ("Should have obtained video_window_xid by now!");
  }

  gst_message_unref (message);
  return GST_BUS_DROP;
}

static void
video_widget_realize_cb (GtkWidget * widget, gpointer data)
{
#ifdef GDK_WINDOWING_X11
  video_window_xid = GDK_WINDOW_XID (widget->window);
#endif
}

static GtkWidget *
setup_gtk_window (void)
{
  GtkWidget *video_window;
  GtkWidget *app_window;

  app_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  video_window = gtk_drawing_area_new ();
  g_signal_connect (video_window, "realize",
      G_CALLBACK (video_widget_realize_cb), NULL);
  gtk_widget_set_double_buffered (video_window, FALSE);

  gtk_container_add (GTK_CONTAINER (app_window), video_window);
  gtk_widget_show_all (app_window);

  gtk_widget_realize (app_window);
  g_assert (video_window_xid != 0);

  return app_window;
}

/* Datastructure to share the state we are interested in between
 * prepare and render function. */
typedef struct
{
  gboolean valid;
  int width;
  int height;
} CairoOverlayState;

/* Store the information from the caps that we are interested in. */
static void
prepare_overlay (GstElement * overlay, GstCaps * caps, gpointer user_data)
{
  CairoOverlayState *state = (CairoOverlayState *) user_data;

  gst_video_format_parse_caps (caps, NULL, &state->width, &state->height);
  state->valid = TRUE;
}

/* Draw the overlay. 
 * This function draws a cute "beating" heart. */
static void
draw_overlay (GstElement * overlay, cairo_t * cr, guint64 timestamp,
    guint64 duration, gpointer user_data)
{
  CairoOverlayState *s = (CairoOverlayState *) user_data;
  double scale;

  if (!s->valid)
    return;

  scale = 2 * (((timestamp / (int) 1e7) % 70) + 30) / 100.0;
  cairo_translate (cr, s->width / 2, (s->height / 2) - 30);
  cairo_scale (cr, scale, scale);

  cairo_move_to (cr, 0, 0);
  cairo_curve_to (cr, 0, -30, -50, -30, -50, 0);
  cairo_curve_to (cr, -50, 30, 0, 35, 0, 60);
  cairo_curve_to (cr, 0, 35, 50, 30, 50, 0);
  cairo_curve_to (cr, 50, -30, 0, -30, 0, 0);
  cairo_set_source_rgba (cr, 0.9, 0.0, 0.1, 0.7);
  cairo_fill (cr);
}

static GstElement *
setup_gst_pipeline (CairoOverlayState * overlay_state)
{
  GstElement *pipeline;
  GstElement *cairo_overlay;
  GstElement *source, *adaptor1, *adaptor2, *sink;
  GstBus *bus;

  pipeline = gst_pipeline_new ("cairo-overlay-example");

  /* Adaptors needed because cairooverlay only supports ARGB data */
  source = gst_element_factory_make ("videotestsrc", "source");
  adaptor1 = gst_element_factory_make ("ffmpegcolorspace", "adaptor1");
  cairo_overlay = gst_element_factory_make ("cairooverlay", "overlay");
  adaptor2 = gst_element_factory_make ("ffmpegcolorspace", "adaptor2");
  sink = gst_element_factory_make ("xvimagesink", "sink");

  /* If failing, the element could not be created */
  g_assert (cairo_overlay);

  /* Hook up the neccesary signals for cairooverlay */
  g_signal_connect (cairo_overlay, "draw",
      G_CALLBACK (draw_overlay), overlay_state);
  g_signal_connect (cairo_overlay, "caps-changed",
      G_CALLBACK (prepare_overlay), overlay_state);

  gst_bin_add_many (GST_BIN (pipeline), source, adaptor1,
      cairo_overlay, adaptor2, sink, NULL);

  if (!gst_element_link_many (source, adaptor1,
          cairo_overlay, adaptor2, sink, NULL)) {
    g_warning ("Failed to link elements!");
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, NULL);
  gst_object_unref (bus);

  return pipeline;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GstElement *pipeline;
  CairoOverlayState *overlay_state;

  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);

  window = setup_gtk_window ();
  overlay_state = g_new (CairoOverlayState, 1);
  pipeline = setup_gst_pipeline (overlay_state);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_main ();

  gst_object_unref (pipeline);
  gtk_widget_destroy (GTK_WIDGET (window));
  g_free (overlay_state);
}
