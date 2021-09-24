/* GStreamer unit tests for avdemux_ape
 *
 * Copyright (C) 2009 Tim-Philipp Müller  <tim centricular net>
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

#include <gst/check/gstcheck.h>

#include <gst/gst.h>

typedef void (CheckTagsFunc) (const GstTagList * tags, const gchar * file);

static void
pad_added_cb (GstElement * decodebin, GstPad * pad, GstBin * pipeline)
{
  GstElement *sink;

  sink = gst_bin_get_by_name (pipeline, "fakesink");
  fail_unless (gst_element_link (decodebin, sink));
  gst_object_unref (sink);

  gst_element_set_state (sink, GST_STATE_PAUSED);
}

static GstBusSyncReply
error_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    const gchar *file = (const gchar *) user_data;
    GError *err = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error (msg, &err, &dbg);
    g_error ("ERROR for %s: %s\n%s\n", file, err->message, dbg);
  }

  return GST_BUS_PASS;
}

static GstPadProbeReturn
event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstTagList **p_tags = user_data;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
    GST_INFO ("tag event: %" GST_PTR_FORMAT, event);
    if (*p_tags == NULL) {
      GstTagList *event_tags;

      GST_INFO ("first tag, saving");
      gst_event_parse_tag (event, &event_tags);
      *p_tags = gst_tag_list_copy (event_tags);
    }
  }
  return GST_PAD_PROBE_OK;      /* keep the data */
}

/* FIXME: push_mode not used currently */
static GstTagList *
read_tags_from_file (const gchar * file, gboolean push_mode)
{
  GstStateChangeReturn state_ret;
  GstTagList *tags = NULL;
  GstElement *sink, *src, *dec, *pipeline;
  GstBus *bus;
  GstPad *pad;
  gchar *path;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL, "Failed to create pipeline!");

  src = gst_element_factory_make ("filesrc", "filesrc");
  fail_unless (src != NULL, "Failed to create filesrc!");

  dec = gst_element_factory_make ("decodebin", "decodebin");
  fail_unless (dec != NULL, "Failed to create decodebin!");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Failed to create fakesink!");

  bus = gst_element_get_bus (pipeline);

  /* kids, don't use a sync handler for this at home, really; we do because
   * we just want to abort and nothing else */
  gst_bus_set_sync_handler (bus, error_cb, (gpointer) file, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, dec, sink, NULL);
  gst_element_link_many (src, dec, NULL);

  path = g_build_filename (GST_TEST_FILES_PATH, file, NULL);
  GST_LOG ("reading file '%s'", path);
  g_object_set (src, "location", path, NULL);

  /* can't link uridecodebin and sink yet, do that later */
  g_signal_connect (dec, "pad-added", G_CALLBACK (pad_added_cb), pipeline);

  /* we want to make sure there's a tag event coming out of avdemux_ape
   * (ie. the one apedemux generated) */
  pad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, event_probe,
      &tags, NULL);
  gst_object_unref (pad);

  state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  if (state_ret == GST_STATE_CHANGE_ASYNC) {
    GST_LOG ("waiting for pipeline to reach PAUSED state");
    state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
    fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
  }

  GST_LOG ("PAUSED, let's retrieve our tags");

  fail_unless (tags != NULL, "Expected tag event! (%s)", file);

  gst_object_unref (bus);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);

  g_free (path);

  GST_INFO ("%s: tags = %" GST_PTR_FORMAT, file, tags);
  return tags;
}

static void
run_check_for_file (const gchar * filename, CheckTagsFunc * check_func)
{
  GstTagList *tags;

  /* first, pull-based */
  tags = read_tags_from_file (filename, FALSE);
  fail_unless (tags != NULL, "Failed to extract tags from '%s'", filename);
  check_func (tags, filename);
  gst_tag_list_unref (tags);
}

#define tag_list_has_tag(taglist,tag) \
    (gst_tag_list_get_value_index((taglist),(tag),0) != NULL)

/* just make sure avdemux_ape forwarded the tags extracted by apedemux
 * (should be the first tag list / tag event too) */
static void
check_for_apedemux_tags (const GstTagList * tags, const gchar * file)
{
  gchar *artist = NULL;

  fail_unless (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &artist));
  fail_unless (artist != NULL);
  fail_unless_equals_string (artist, "Marvin Gaye");
  g_free (artist);

  fail_unless (tag_list_has_tag (tags, GST_TAG_CONTAINER_FORMAT));

  GST_LOG ("all good");
}

GST_START_TEST (test_tag_caching)
{
#define MIN_VERSION GST_VERSION_MAJOR, GST_VERSION_MINOR, 0

  if (!gst_registry_check_feature_version (gst_registry_get (), "apedemux",
          MIN_VERSION)
      || !gst_registry_check_feature_version (gst_registry_get (), "decodebin",
          MIN_VERSION)) {
    g_printerr ("Skipping test_tag_caching: required element apedemux or "
        "decodebin element not found\n");
    return;
  }

  run_check_for_file ("586957.ape", check_for_apedemux_tags);
}

GST_END_TEST;

static Suite *
avdemux_ape_suite (void)
{
  Suite *s = suite_create ("avdemux_ape");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_tag_caching);

  return s;
}

GST_CHECK_MAIN (avdemux_ape)
