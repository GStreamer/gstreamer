/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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

#include <gst/play/play.h>

static GMainLoop *loop = NULL;
static gint64 length = 0;

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      g_assert (gst_tag_list_get_string_index (list, tag, i, &str));
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("%15s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("               : %s\n", str);
    }

    g_free (str);
  }
}

static void
got_found_tag (GstPlay * play, GstElement * source, GstTagList * tag_list)
{
  gst_tag_list_foreach (tag_list, print_tag, NULL);
}

static void
got_time_tick (GstPlay * play, gint64 time_nanos)
{
  g_print ("time tick %f\n", time_nanos / (float) GST_SECOND);
}

static void
got_stream_length (GstPlay * play, gint64 length_nanos)
{
  g_print ("got length %llu\n", length_nanos);
  length = length_nanos;
}

static void
got_video_size (GstPlay * play, gint width, gint height)
{
  g_print ("got video size %d, %d\n", width, height);
}

static void
got_eos (GstPlay * play)
{
  g_print ("End Of Stream\n");
  g_main_loop_quit (loop);
}

static gboolean
seek_timer (GstPlay * play)
{
  gst_play_seek_to_time (play, length / 2);
  return FALSE;
}

static gboolean
idle_iterate (GstPlay * play)
{
  gst_bin_iterate (GST_BIN (play));
  return (GST_STATE (GST_ELEMENT (play)) == GST_STATE_PLAYING);
}

int
main (int argc, char *argv[])
{
  GstPlay *play;
  GstElement *data_src, *video_sink, *audio_sink, *vis_element;
  GError *error = NULL;

  /* Initing GStreamer library */
  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <video filename>\n", argv[0]);
    exit (-1);
  }

  loop = g_main_loop_new (NULL, FALSE);

  /* Creating the GstPlay object */
  play = gst_play_new (&error);
  if (error) {
    g_print ("Error: could not create play object:\n%s\n", error->message);
    g_error_free (error);
    return 1;
  }

  /* Getting default audio and video plugins from GConf */
  audio_sink = gst_element_factory_make ("osssink", "audio_sink");
  video_sink = gst_element_factory_make ("ximagesink", "video_sink");
  vis_element = gst_element_factory_make ("goom", "vis_element");
  data_src = gst_element_factory_make ("gnomevfssrc", "source");

  /* Let's send them to GstPlay object */
  gst_play_set_audio_sink (play, audio_sink);
  gst_play_set_video_sink (play, video_sink);
  gst_play_set_data_src (play, data_src);
  gst_play_set_visualization (play, vis_element);

  /* Setting location we want to play */
  gst_play_set_location (play, argv[1]);

  /* Uncomment that line to get an XML dump of the pipeline */
  /* gst_xml_write_file (GST_ELEMENT (play), stdout); */

  g_signal_connect (G_OBJECT (play), "time_tick",
      G_CALLBACK (got_time_tick), NULL);
  g_signal_connect (G_OBJECT (play), "stream_length",
      G_CALLBACK (got_stream_length), NULL);
  g_signal_connect (G_OBJECT (play), "have_video_size",
      G_CALLBACK (got_video_size), NULL);
  g_signal_connect (G_OBJECT (play), "found_tag",
      G_CALLBACK (got_found_tag), NULL);
  g_signal_connect (G_OBJECT (play), "error",
      G_CALLBACK (gst_element_default_error), NULL);
  g_signal_connect (G_OBJECT (play), "eos", G_CALLBACK (got_eos), NULL);

  /* Change state to PLAYING */
  gst_element_set_state (GST_ELEMENT (play), GST_STATE_PLAYING);

  g_idle_add ((GSourceFunc) idle_iterate, play);
  g_timeout_add (20000, (GSourceFunc) seek_timer, play);

  g_main_loop_run (loop);

  g_print ("setting pipeline to ready\n");

  gst_element_set_state (GST_ELEMENT (play), GST_STATE_READY);

  /* unref 
     gst_object_unref (GST_OBJECT (play)); */

  exit (0);
}
