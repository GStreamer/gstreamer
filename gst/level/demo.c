/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * demo.c: sample application to display VU meter-like output of level
 * Copyright (C) 2003
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <gst/gst.h>
#include <gtk/gtk.h>

/* global array for the scale widgets, we'll assume stereo */
GtkWidget *elapsed;
GtkWidget *scale[2][3];

static void
level_callback (GstElement * element, gdouble time, gint channel,
    gdouble rms, gdouble peak, gdouble decay)
{
  gchar *label;

  label = g_strdup_printf ("%.3f", time);
  gtk_label_set (GTK_LABEL (elapsed), label);
  g_free (label);
  gtk_range_set_value (GTK_RANGE (scale[channel][0]), rms);
  gtk_range_set_value (GTK_RANGE (scale[channel][1]), peak);
  gtk_range_set_value (GTK_RANGE (scale[channel][2]), decay);
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
setup_gui ()
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *label, *hbox;
  int c;

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

  for (c = 0; c < 2; ++c) {
    /* RMS */
    hbox = gtk_hbox_new (TRUE, 0);
    label = gtk_label_new ("RMS");
    gtk_container_add (GTK_CONTAINER (hbox), label);
    scale[c][0] = gtk_hscale_new_with_range (-90.0, 0.0, 0.2);
    gtk_widget_set_size_request (scale[c][0], 100, -1);
    gtk_container_add (GTK_CONTAINER (hbox), scale[c][0]);
    gtk_container_add (GTK_CONTAINER (vbox), hbox);
    /* peak */
    hbox = gtk_hbox_new (TRUE, 0);
    label = gtk_label_new ("peak");
    gtk_container_add (GTK_CONTAINER (hbox), label);
    scale[c][1] = gtk_hscale_new_with_range (-90.0, 0.0, 0.2);
    gtk_widget_set_size_request (scale[c][1], 100, -1);
    gtk_container_add (GTK_CONTAINER (hbox), scale[c][1]);
    gtk_container_add (GTK_CONTAINER (vbox), hbox);
    /* decay */
    hbox = gtk_hbox_new (TRUE, 0);
    label = gtk_label_new ("decaying peek");
    gtk_container_add (GTK_CONTAINER (hbox), label);
    scale[c][2] = gtk_hscale_new_with_range (-90.0, 0.0, 0.2);
    gtk_widget_set_size_request (scale[c][2], 100, -1);
    gtk_container_add (GTK_CONTAINER (hbox), scale[c][2]);
    gtk_container_add (GTK_CONTAINER (vbox), hbox);
  }

  gtk_widget_show_all (GTK_WIDGET (window));
}

int
main (int argc, char *argv[])
{

  GstElement *pipeline = NULL;
  GError *error = NULL;
  GstElement *level;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  pipeline = gst_parse_launchv ((const gchar **) &argv[1], &error);
  if (error) {
    g_print ("pipeline could not be constructed: %s\n", error->message);
    g_print ("Please give a complete pipeline  with a 'level' element.\n");
    g_print ("Example: sinesrc ! level ! osssink\n");
    g_error_free (error);
    return 1;
  }

  level = gst_bin_get_by_name (GST_BIN (pipeline), "level0");
  if (level == NULL) {
    g_print ("Please give a pipeline with a 'level' element in it\n");
    return 1;
  }

  g_object_set (level, "signal", TRUE, NULL);
  g_signal_connect (level, "level", G_CALLBACK (level_callback), NULL);


  /* setup GUI */
  setup_gui ();

  /* connect level signal */

  /* go to main loop */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_idle_add (idler, pipeline);

  gtk_main ();

  return 0;
}
