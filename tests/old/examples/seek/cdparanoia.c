#include <stdlib.h>
#include <gst/gst.h>
#include <string.h>

static void
get_position_info (GstElement * cdparanoia)
{
  GstFormat track_format;
  const GstFormat *formats;
  GstPad *pad;

  track_format = gst_format_get_by_nick ("track");
  g_assert (track_format != 0);

  pad = gst_element_get_pad (cdparanoia, "src");
  formats = gst_pad_get_formats (pad);

  while (*formats) {
    const GstFormatDefinition *definition;
    GstFormat format;
    gint64 position;
    gboolean res;

    definition = gst_format_get_details (*formats);

    format = *formats;
    res = gst_pad_query (pad, GST_QUERY_POSITION, &format, &position);

    if (format == GST_FORMAT_TIME) {
      position /= GST_SECOND;
      g_print ("%s: %lld:%02lld", definition->nick, position / 60,
	  position % 60);
    } else {
      g_print ("%s: %lld", definition->nick, position);
    }

    formats++;
    if (*formats) {
      g_print (", ");
    }
  }
  g_print ("\r");
}

static void
get_track_info (GstElement * cdparanoia)
{
  GstFormat track_format;
  gint64 total_tracks = 0, total_time = 0;
  GstPad *pad;
  const GstFormat *formats;
  gint i;
  gint64 time_count = 0;

  track_format = gst_format_get_by_nick ("track");
  g_assert (track_format != 0);

  pad = gst_element_get_pad (cdparanoia, "src");
  formats = gst_pad_get_formats (pad);

  /* we loop over all supported formats and report the total
   * number of them */
  while (*formats) {
    const GstFormatDefinition *definition;
    gint64 total;
    GstFormat format;
    gboolean res;

    definition = gst_format_get_details (*formats);

    format = *formats;
    res = gst_pad_query (pad, GST_QUERY_TOTAL, &format, &total);
    if (res) {
      if (format == GST_FORMAT_TIME) {
	total /= GST_SECOND;
	g_print ("%s total: %lld:%02lld\n", definition->nick, total / 60,
	    total % 60);
      } else
	g_print ("%s total: %lld\n", definition->nick, total);

      if (format == track_format)
	total_tracks = total;
      else if (format == GST_FORMAT_TIME)
	total_time = total;
    } else
      g_print ("failed to get %s total\n", definition->nick);

    formats++;
  }

  /* then we loop over all the tracks to get more info.
   * since pad_convert always works from 0, the time from track 1 needs
   * to be substracted from track 2 */
  for (i = 0; i <= total_tracks; i++) {
    gint64 time;
    gboolean res;

    if (i < total_tracks) {
      GstFormat format;

      format = GST_FORMAT_TIME;
      res = gst_pad_convert (pad, track_format, i, &format, &time);
      time /= GST_SECOND;
    } else {
      time = total_time;
      res = TRUE;
    }

    if (res) {
      /* for the first track (i==0) we wait until we have the
       * time of the next track */
      if (i > 0) {
	gint64 length = time - time_count;

	g_print ("track %d: %lld:%02lld -> %lld:%02lld, length: %lld:%02lld\n",
	    i - 1,
	    time_count / 60, time_count % 60,
	    time / 60, time % 60, length / 60, length % 60);
      }
    } else {
      g_print ("could not get time for track %d\n", i);
    }

    time_count = time;
  }
}

int
main (int argc, char **argv)
{
  GstElement *pipeline;
  GstElement *cdparanoia;
  GstElement *osssink;
  GstPad *pad;
  GstFormat track_format;
  GstEvent *event;
  gint count;
  gboolean res;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  cdparanoia = gst_element_factory_make ("cdparanoia", "cdparanoia");
  g_assert (cdparanoia);
  g_object_set (G_OBJECT (cdparanoia), "paranoia_mode", 0, NULL);

  osssink = gst_element_factory_make ("osssink", "osssink");
  g_assert (osssink);

  gst_bin_add (GST_BIN (pipeline), cdparanoia);
  gst_bin_add (GST_BIN (pipeline), osssink);

  gst_element_link_pads (cdparanoia, "src", osssink, "sink");

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  /* now we go into probe mode */
  get_track_info (cdparanoia);

  track_format = gst_format_get_by_nick ("track");
  g_assert (track_format != 0);

  pad = gst_element_get_pad (cdparanoia, "src");
  g_assert (pad);

  g_print ("playing from track 3\n");
  /* seek to track3 */
  event = gst_event_new_seek (track_format |
      GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, 3);

  res = gst_pad_send_event (pad, event);
  if (!res)
    g_warning ("seek failed");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  count = 0;
  while (gst_bin_iterate (GST_BIN (pipeline))) {
    get_position_info (cdparanoia);
    if (count++ > 500)
      break;
  }
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  g_print ("\nplaying from second 25 to second 29\n");
  /* seek to some seconds */
  event = gst_event_new_segment_seek (GST_FORMAT_TIME |
      GST_SEEK_METHOD_SET |
      GST_SEEK_FLAG_FLUSH, 25 * GST_SECOND, 29 * GST_SECOND);
  res = gst_pad_send_event (pad, event);
  if (!res)
    g_warning ("seek failed");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline))) {
    get_position_info (cdparanoia);
  }
  g_print ("\n");

  /* shutdown everything again */
  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
