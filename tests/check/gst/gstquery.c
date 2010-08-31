/* GStreamer
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


#include <gst/check/gstcheck.h>
GST_START_TEST (create_queries)
{
  GstQuery *query;

  /* POSITION */
  {
    GstFormat format;
    gint64 position;

    query = gst_query_new_position (GST_FORMAT_TIME);
    fail_if (query == NULL);
    fail_unless (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

    gst_query_parse_position (query, &format, NULL);
    fail_if (format != GST_FORMAT_TIME);

    gst_query_set_position (query, GST_FORMAT_TIME, 0xdeadbeaf);

    gst_query_parse_position (query, &format, &position);
    fail_if (format != GST_FORMAT_TIME);
    fail_if (position != 0xdeadbeaf);

    gst_query_unref (query);
  }
  /* DURATION */
  {
    GstFormat format;
    gint64 duration;

    query = gst_query_new_duration (GST_FORMAT_TIME);
    fail_if (query == NULL);
    fail_unless (GST_QUERY_TYPE (query) == GST_QUERY_DURATION);

    gst_query_parse_duration (query, &format, NULL);
    fail_if (format != GST_FORMAT_TIME);

    gst_query_set_duration (query, GST_FORMAT_TIME, 0xdeadbeaf);

    gst_query_parse_duration (query, &format, &duration);
    fail_if (format != GST_FORMAT_TIME);
    fail_if (duration != 0xdeadbeaf);

    gst_query_unref (query);
  }
  /* BUFFERING RANGES */
  {
    gint64 start, stop;

    query = gst_query_new_buffering (GST_FORMAT_PERCENT);
    fail_if (query == NULL);
    fail_unless (GST_QUERY_TYPE (query) == GST_QUERY_BUFFERING);

    fail_unless (gst_query_add_buffering_range (query, 0, 20));
    fail_unless (gst_query_add_buffering_range (query, 25, 30));

    /* check incoherent range insertion */
    fail_if (gst_query_add_buffering_range (query, 10, 15));
    fail_if (gst_query_add_buffering_range (query, 50, 40));

    fail_unless (gst_query_get_n_buffering_ranges (query) == 2);

    fail_unless (gst_query_parse_nth_buffering_range (query, 0, &start, &stop));
    fail_unless (start == 0);
    fail_unless (stop == 20);

    fail_unless (gst_query_parse_nth_buffering_range (query, 1, &start, &stop));
    fail_unless (start == 25);
    fail_unless (stop == 30);

    gst_query_unref (query);
  }
  {
    /* FIXME make tests for:
     *
     * LATENCY
     * JITTER
     * RATE
     * SEEKING
     * SEGMENT
     * CONVERT
     */
  }
  /* SEGMENT */
  {
    gdouble rate;
    GstFormat format;
    gint64 start, stop;

    format = GST_FORMAT_BYTES;
    query = gst_query_new_segment (format);

    fail_if (query == NULL);
    fail_unless (GST_QUERY_TYPE (query) == GST_QUERY_SEGMENT);

    gst_query_parse_segment (query, &rate, &format, &start, &stop);

    /* see if empty gives undefined formats */
    fail_if (rate != 0.0);
    fail_if (format != GST_FORMAT_BYTES);
    fail_if (start != -1);
    fail_if (stop != -1);

    /* change all values */
    gst_query_set_segment (query, 2.0, GST_FORMAT_TIME, 1 * GST_SECOND,
        3 * GST_SECOND);

    gst_query_parse_segment (query, &rate, &format, &start, &stop);

    /* see if the values were changed */
    fail_if (rate != 2.0);
    fail_if (format != GST_FORMAT_TIME);
    fail_if (start != 1 * GST_SECOND);
    fail_if (stop != 3 * GST_SECOND);

    gst_query_unref (query);
  }

  /* FORMATS */
  {
    guint size;
    GstFormat format;

    query = gst_query_new_formats ();
    fail_if (query == NULL);
    fail_unless (GST_QUERY_TYPE (query) == GST_QUERY_FORMATS);

    /* empty */
    gst_query_parse_formats_length (query, &size);
    fail_if (size != 0);

    /* see if empty gives undefined formats */
    gst_query_parse_formats_nth (query, 0, &format);
    fail_if (format != GST_FORMAT_UNDEFINED);
    gst_query_parse_formats_nth (query, 1, &format);
    fail_if (format != GST_FORMAT_UNDEFINED);

    /* set 2 formats */
    gst_query_set_formats (query, 2, GST_FORMAT_TIME, GST_FORMAT_BYTES);

    gst_query_parse_formats_length (query, &size);
    fail_if (size != 2);

    format = GST_FORMAT_UNDEFINED;

    gst_query_parse_formats_nth (query, 0, &format);
    fail_if (format != GST_FORMAT_TIME);
    gst_query_parse_formats_nth (query, 1, &format);
    fail_if (format != GST_FORMAT_BYTES);

    /* out of bounds, should return UNDEFINED */
    gst_query_parse_formats_nth (query, 2, &format);
    fail_if (format != GST_FORMAT_UNDEFINED);

    /* overwrite with 3 formats */
    gst_query_set_formats (query, 3, GST_FORMAT_TIME, GST_FORMAT_BYTES,
        GST_FORMAT_PERCENT);

    gst_query_parse_formats_length (query, &size);
    fail_if (size != 3);

    gst_query_parse_formats_nth (query, 2, &format);
    fail_if (format != GST_FORMAT_PERCENT);

    /* create one from an array */
    {
      static GstFormat formats[] = {
        GST_FORMAT_TIME,
        GST_FORMAT_BYTES,
        GST_FORMAT_PERCENT
      };
      gst_query_set_formatsv (query, 3, formats);

      gst_query_parse_formats_length (query, &size);
      fail_if (size != 3);

      gst_query_parse_formats_nth (query, 0, &format);
      fail_if (format != GST_FORMAT_TIME);
      gst_query_parse_formats_nth (query, 2, &format);
      fail_if (format != GST_FORMAT_PERCENT);
    }
    gst_query_unref (query);
  }
}

GST_END_TEST;

GST_START_TEST (test_queries)
{
  GstBin *bin;
  GstElement *src, *sink;
  GstStateChangeReturn ret;
  GstPad *pad;
  GstQuery *dur, *pos;

  fail_unless ((bin = (GstBin *) gst_pipeline_new (NULL)) != NULL,
      "Could not create pipeline");
  fail_unless ((src = gst_element_factory_make ("fakesrc", NULL)) != NULL,
      "Could not create fakesrc");
  g_object_set (src, "datarate", 200, "sizetype", 2, NULL);

  fail_unless ((sink = gst_element_factory_make ("fakesink", NULL)) != NULL,
      "Could not create fakesink");
  g_object_set (sink, "sync", TRUE, NULL);
  fail_unless ((dur = gst_query_new_duration (GST_FORMAT_BYTES)) != NULL,
      "Could not prepare duration query");
  fail_unless ((pos = gst_query_new_position (GST_FORMAT_BYTES)) != NULL,
      "Could not prepare position query");

  fail_unless (gst_bin_add (bin, src), "Could not add src to bin");
  fail_unless (gst_bin_add (bin, sink), "Could not add sink to bin");
  fail_unless (gst_element_link (src, sink), "could not link src and sink");

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  fail_if (ret == GST_STATE_CHANGE_FAILURE, "Failed to set pipeline PLAYING");
  if (ret == GST_STATE_CHANGE_ASYNC)
    gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_CLOCK_TIME_NONE);

  /* Query the bin */
  fail_unless (gst_element_query (GST_ELEMENT (bin), pos),
      "Could not query pipeline position");
  fail_unless (gst_element_query (GST_ELEMENT (bin), dur),
      "Could not query pipeline duration");

  /* Query elements */
  fail_unless (gst_element_query (GST_ELEMENT (src), pos),
      "Could not query position of fakesrc");
  fail_unless (gst_element_query (GST_ELEMENT (src), pos),
      "Could not query duration of fakesrc");

  fail_unless (gst_element_query (GST_ELEMENT (sink), pos),
      "Could not query position of fakesink");
  fail_unless (gst_element_query (GST_ELEMENT (sink), pos),
      "Could not query duration of fakesink");

  /* Query pads */
  fail_unless ((pad = gst_element_get_static_pad (src, "src")) != NULL,
      "Could not get source pad of fakesrc");
  fail_unless (gst_pad_query (pad, pos),
      "Could not query position of fakesrc src pad");
  fail_unless (gst_pad_query (pad, dur),
      "Could not query duration of fakesrc src pad");
  gst_object_unref (pad);

  /* We don't query the sink pad of fakesink, it doesn't 
   * handle downstream queries atm, but it might later, who knows? */

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  fail_if (ret == GST_STATE_CHANGE_FAILURE, "Failed to set pipeline NULL");
  if (ret == GST_STATE_CHANGE_ASYNC)
    gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_query_unref (dur);
  gst_query_unref (pos);
  gst_object_unref (bin);
}

GST_END_TEST;

static Suite *
gst_query_suite (void)
{
  Suite *s = suite_create ("GstQuery");
  TCase *tc_chain = tcase_create ("queries");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, create_queries);
  tcase_add_test (tc_chain, test_queries);
  return s;
}

GST_CHECK_MAIN (gst_query);
