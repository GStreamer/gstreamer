/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * plot.c: output data points to be graphed with gnuplot
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

gboolean got_channel[2] = { FALSE, FALSE };	/* to see if we got the signal for this one yet */
gint channels = 0;		/* guess at how many channels there are */
gdouble last_time = 0.0;	/* time of last signal */
gdouble values[2][3];		/* array of levels from which to print */

static void
level_callback (GstElement * element, gdouble time, gint channel,
    gdouble rms, gdouble peak, gdouble decay)
{
  int i = 0, j = 0;
  gboolean got_all = FALSE;

  if (channel + 1 > channels)
    channels = channel + 1;

  /* reset got_channel if this is a new time point */
  if (time > last_time) {
    for (i = 0; i < channels; ++i)
      got_channel[i] = FALSE;
    last_time = time;
  }

  /* store values */
  got_channel[channel] = TRUE;
  values[channel][0] = rms;
  values[channel][1] = peak;
  values[channel][2] = decay;

  /* check if we have all channels, and output if we do */
  /* FIXME: this fails on the first, no ? */
  got_all = TRUE;
  for (i = 0; i < channels; ++i)
    if (!got_channel[i])
      got_all = FALSE;
  if (got_all) {
    g_print ("%f ", time);
    for (i = 0; i < channels; ++i)
      for (j = 0; j < 3; ++j)
	g_print ("%f ", values[i][j]);
    g_print ("\n");
  }
}

static gboolean
idler (gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);

  if (gst_bin_iterate (GST_BIN (pipeline)))
    return TRUE;

  gtk_main_quit ();
  return FALSE;
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


  /* go to main loop */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_idle_add (idler, pipeline);

  gtk_main ();

  return 0;
}
