/* GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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
#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gtk/gtk.h>

#ifndef DEFAULT_AUDIOSRC
#define DEFAULT_AUDIOSRC "alsasrc"
#endif

static guint spect_height = 64;
static guint spect_bands = 256;
static gfloat height_scale = 1.0;

static GtkWidget *drawingarea = NULL;

static void
on_window_destroy (GtkObject * object, gpointer user_data)
{
  drawingarea = NULL;
  gtk_main_quit ();
}

gboolean
on_configure_event (GtkWidget * widget, GdkEventConfigure * event,
    gpointer user_data)
{
  GstElement *spectrum = GST_ELEMENT (user_data);

  /*GST_INFO ("%d x %d", event->width, event->height); */
  spect_height = event->height;
  height_scale = event->height / 64.0;
  spect_bands = event->width;

  g_object_set (G_OBJECT (spectrum), "bands", spect_bands, NULL);
  return FALSE;
}

/* draw frequency spectrum as a bunch of bars */
static void
draw_spectrum (gfloat * data)
{
  gint i;
  GdkRectangle rect = { 0, 0, spect_bands, spect_height };

  if (!drawingarea)
    return;

  gdk_window_begin_paint_rect (drawingarea->window, &rect);
  gdk_draw_rectangle (drawingarea->window, drawingarea->style->black_gc,
      TRUE, 0, 0, spect_bands, spect_height);
  for (i = 0; i < spect_bands; i++) {
    gdk_draw_rectangle (drawingarea->window, drawingarea->style->white_gc,
        TRUE, i, -data[i], 1, spect_height + data[i]);
  }
  gdk_window_end_paint (drawingarea->window);
}

/* receive spectral data from element message */
gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "spectrum") == 0) {
      gfloat spect[spect_bands];
      const GValue *list;
      const GValue *value;
      guint i;

      list = gst_structure_get_value (s, "magnitude");
      for (i = 0; i < spect_bands; ++i) {
        value = gst_value_list_get_value (list, i);
        spect[i] = height_scale * g_value_get_float (value);
      }
      draw_spectrum (spect);
    }
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src, *spectrum, *sink;
  GstBus *bus;
  GtkWidget *appwindow;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  src = gst_element_factory_make (DEFAULT_AUDIOSRC, "src");

  spectrum = gst_element_factory_make ("spectrum", "spectrum");
  g_object_set (G_OBJECT (spectrum), "bands", spect_bands, "threshold", -80,
      "message", TRUE, NULL);

  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add_many (GST_BIN (bin), src, spectrum, sink, NULL);
  if (!gst_element_link_many (src, spectrum, sink, NULL)) {
    fprintf (stderr, "can't link elements\n");
    exit (1);
  }

  bus = gst_element_get_bus (bin);
  gst_bus_add_watch (bus, message_handler, NULL);
  gst_object_unref (bus);

  appwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (appwindow), "destroy",
      G_CALLBACK (on_window_destroy), NULL);

  drawingarea = gtk_drawing_area_new ();
  gtk_widget_set_size_request (drawingarea, spect_bands, spect_height);
  g_signal_connect (G_OBJECT (drawingarea), "configure-event",
      G_CALLBACK (on_configure_event), (gpointer) spectrum);
  gtk_container_add (GTK_CONTAINER (appwindow), drawingarea);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (bin, GST_STATE_PLAYING);
  gtk_main ();
  gst_element_set_state (bin, GST_STATE_NULL);

  gst_object_unref (bin);

  return 0;
}
