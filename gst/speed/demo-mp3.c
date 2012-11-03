/*
 * GStreamer
 * Copyright (C) 2002 Andy Wingo <wingo at pobox dot com>
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
#include "config.h"
#endif
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

static GtkWidget *poslabel;     /* NULL */

void
set_speed (GtkAdjustment * adj, gpointer data)
{
  GstElement *speed = GST_ELEMENT (data);

  g_object_set (speed, "speed", adj->value, NULL);
}

static gboolean
time_tick_cb (GstElement * audiosink)
{
  GstFormat format = GST_FORMAT_TIME;
  guint64 total, pos;

  if (gst_element_query (audiosink, GST_QUERY_TOTAL, &format, &total)
      && gst_element_query (audiosink, GST_QUERY_POSITION, &format, &pos)) {
    guint t_min, t_sec, p_min, p_sec;
    gchar *s;

    t_min = (guint) (total / (GST_SECOND * 60));
    t_sec = (guint) ((total % (GST_SECOND * 60)) / GST_SECOND);
    p_min = (guint) (pos / (GST_SECOND * 60));
    p_sec = (guint) ((pos % (GST_SECOND * 60)) / GST_SECOND);

    s = g_strdup_printf ("%u:%02u / %u:%02u", p_min, p_sec, t_min, t_sec);
    gtk_label_set_text (GTK_LABEL (poslabel), s);
    g_free (s);
  }

  return TRUE;                  /* call again */
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *vbox, *hscale, *button, *hbbox;
  GstElement *filesrc, *mad, *audioconvert, *speed, *audiosink, *pipeline;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <your.mp3>\n", argv[0]);
    exit (-1);
  }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete-event", G_CALLBACK (gtk_main_quit), NULL);
  gtk_window_set_default_size (GTK_WINDOW (window), 400, 80);
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  hscale = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 0.1, 4.0,
              0.1, 0.0, 0.0)));
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);
  hbbox = gtk_hbutton_box_new ();
  button = gtk_button_new_from_stock (GTK_STOCK_QUIT);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_container_add (GTK_CONTAINER (hbbox), button);
  poslabel = gtk_label_new (NULL);
  gtk_box_pack_start (GTK_BOX (vbox), poslabel, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), hscale, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), hbbox, FALSE, FALSE, 6);
  g_signal_connect (button, "clicked", G_CALLBACK (gtk_main_quit), NULL);

  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  mad = gst_element_factory_make ("mad", "mad");
  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert0");
  speed = gst_element_factory_make ("speed", "speed");
  audiosink = gst_element_factory_make (DEFAULT_AUDIOSINK, "audiosink");

  g_signal_connect (gtk_range_get_adjustment (GTK_RANGE (hscale)),
      "value_changed", G_CALLBACK (set_speed), speed);

  pipeline = gst_pipeline_new ("app");
  gst_bin_add_many (GST_BIN (pipeline), filesrc, mad, audioconvert, speed,
      audiosink, NULL);
  gst_element_link_many (filesrc, mad, audioconvert, speed, audiosink, NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gtk_widget_show_all (window);

  g_idle_add ((GSourceFunc) gst_bin_iterate, pipeline);
  g_timeout_add (200, (GSourceFunc) time_tick_cb, audiosink);

  gtk_main ();

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
