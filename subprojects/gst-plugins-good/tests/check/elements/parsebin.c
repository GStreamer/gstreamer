/* GStreamer
 * Copyright (C) 2026 Edward Hervey <edward@centricular.com>
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

#include <gst/check/gstcheck.h>
#include <gio/gio.h>

/* giostreamsrc answers no URI query, so each element of the chain parsebin
 * creates its own stream-id, and a parser only creates one when it pushes its
 * first buffer. A chain must thus not be exposed before that happened, as
 * decodebin3 aborts on a stream-start no stream collection covers.
 *
 * The chain needs id3demux and mpegaudioparse to get long enough to show it,
 * which is why this test lives here rather than in gst-plugins-base. */

typedef struct
{
  /* Copies, as the ids are owned by the events they come from */
  GPtrArray *ids;               /* ids published in a stream collection */
  GPtrArray *starts;            /* stream-start ids seen on the pad */
}
Coverage;

static void
handle_event (Coverage * cov, GstEvent * ev)
{
  if (GST_EVENT_TYPE (ev) == GST_EVENT_STREAM_COLLECTION) {
    GstStreamCollection *col = NULL;
    guint i;

    gst_event_parse_stream_collection (ev, &col);
    for (i = 0; col && i < gst_stream_collection_get_size (col); i++) {
      GstStream *stream = gst_stream_collection_get_stream (col, i);
      g_ptr_array_add (cov->ids, g_strdup (gst_stream_get_stream_id (stream)));
    }
  } else if (GST_EVENT_TYPE (ev) == GST_EVENT_STREAM_START) {
    const gchar *stream_id = NULL;

    gst_event_parse_stream_start (ev, &stream_id);
    g_ptr_array_add (cov->starts, g_strdup (stream_id));
  }
}

static GstPadProbeReturn
check_coverage (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  handle_event (user_data, GST_PAD_PROBE_INFO_EVENT (info));
  return GST_PAD_PROBE_OK;
}

static void
on_pad_added (GstElement * parsebin, GstPad * pad, gpointer user_data)
{
  Coverage *cov = user_data;
  GstEvent *ev;

  /* The collection is published before pad-added is emitted, so seed from the
   * sticky event and only watch what comes after. */
  ev = gst_pad_get_sticky_event (pad, GST_EVENT_STREAM_COLLECTION, 0);
  if (ev) {
    handle_event (cov, ev);
    gst_event_unref (ev);
  }

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      check_coverage, user_data, NULL);
}

GST_START_TEST (test_collection_covers_stream_start)
{
  GstElement *pipeline, *src, *parsebin;
  GstBus *bus;
  GstMessage *msg;
  GFile *file;
  GFileInputStream *stream;
  gchar *path;
  Coverage cov = { NULL, NULL };
  guint i;

  cov.ids = g_ptr_array_new_with_free_func (g_free);
  cov.starts = g_ptr_array_new_with_free_func (g_free);

  /* parsebin only ever has dynamic source pads, so let the parser wire the last
   * link for us. */
  pipeline = gst_parse_launch
      ("giostreamsrc name=src ! parsebin name=parse ! fakesink", NULL);
  fail_if (pipeline == NULL);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  parsebin = gst_bin_get_by_name (GST_BIN (pipeline), "parse");
  g_signal_connect (parsebin, "pad-added", G_CALLBACK (on_pad_added), &cov);

  path = g_build_filename (GST_TEST_FILES_PATH, "id3-tagged.mp3", NULL);
  file = g_file_new_for_path (path);
  g_free (path);
  stream = g_file_read (file, NULL, NULL);
  g_object_unref (file);
  fail_if (stream == NULL);
  g_object_set (src, "stream", stream, NULL);
  g_object_unref (stream);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_if (msg == NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
  gst_message_unref (msg);
  gst_object_unref (bus);

  /* Stop before reading what the probes collected */
  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* The rule decodebin3 applies, whatever the relative order of the events */
  fail_unless (cov.starts->len > 0, "no stream-start seen on %s:src_0",
      GST_OBJECT_NAME (parsebin));
  for (i = 0; i < cov.starts->len; i++) {
    const gchar *sid = g_ptr_array_index (cov.starts, i);

    fail_unless (g_ptr_array_find_with_equal_func (cov.ids, sid, g_str_equal,
            NULL), "stream-start %s is not covered by any stream collection",
        sid);
  }

  g_ptr_array_free (cov.starts, TRUE);
  g_ptr_array_free (cov.ids, TRUE);
  gst_object_unref (src);
  gst_object_unref (parsebin);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
parsebin_suite (void)
{
  Suite *s = suite_create ("parsebin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_collection_covers_stream_start);

  return s;
}

GST_CHECK_MAIN (parsebin);
