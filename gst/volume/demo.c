/* GStreamer
 *
 * demo.c: sample application to change the volume of a pipeline
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <math.h>

#include <gst/gst.h>
#include <gtk/gtk.h>

/* global pointer for the scale widget */
GtkWidget *elapsed;
GtkWidget *scale;

#ifndef M_LN10
#define M_LN10 (log(10.0))
#endif

static void
value_changed_callback (GtkWidget * widget, GstElement * volume)
{
  gdouble value;
  gdouble level;

  value = gtk_range_get_value (GTK_RANGE (widget));
  level = exp (value / 20.0 * M_LN10);
  g_print ("Value: %f dB, level: %f\n", value, level);
  g_object_set (volume, "volume", level, NULL);
}

static gboolean
idler (gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);

  g_print ("+");
  if (gst_bin_iterate (GST_BIN (pipeline)))
    return TRUE;
  gtk_main_quit ();
  return FALSE;
}

static void
setup_gui (GstElement * volume)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *label, *hbox;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "destroy", gtk_main_quit, NULL);

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  /* elapsed widget */
  hbox = gtk_hbox_new (TRUE, 0);
  label = gtk_label_new ("Elapsed");
  elapsed = gtk_label_new ("0.000");
  gtk_container_add (GTK_CONTAINER (hbox), label);
  gtk_container_add (GTK_CONTAINER (hbox), elapsed);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);

  /* volume */
  hbox = gtk_hbox_new (TRUE, 0);
  label = gtk_label_new ("volume");
  gtk_container_add (GTK_CONTAINER (hbox), label);
  scale = gtk_hscale_new_with_range (-90.0, 10.0, 0.2);
  gtk_range_set_value (GTK_RANGE (scale), 0.0);
  gtk_widget_set_size_request (scale, 100, -1);
  gtk_container_add (GTK_CONTAINER (hbox), scale);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
  g_signal_connect (scale, "value-changed",
      G_CALLBACK (value_changed_callback), volume);

  gtk_widget_show_all (GTK_WIDGET (window));
}

int
main (int argc, char *argv[])
{

  GstElement *pipeline = NULL;
  GError *error = NULL;
  GstElement *volume;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  pipeline = gst_parse_launchv ((const gchar **) &argv[1], &error);
  if (error) {
    g_print ("pipeline could not be constructed: %s\n", error->message);
    g_print ("Please give a complete pipeline  with a 'volume' element.\n");
    g_print ("Example: sinesrc ! volume ! osssink\n");
    g_error_free (error);
    return 1;
  }

  volume = gst_bin_get_by_name (GST_BIN (pipeline), "volume0");
  if (volume == NULL) {
    g_print ("Please give a pipeline with a 'volume' element in it\n");
    return 1;
  }

  /* setup GUI */
  setup_gui (volume);

  /* go to main loop */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_idle_add (idler, pipeline);

  gtk_main ();

  return 0;
}
