/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#include <gst/app/gstappsink.h>
#include <gst/check/gstcheck.h>
#include <glib/gstdio.h>

typedef struct
{
  GstElement *sink;
} DemuxCtx;

static gboolean
element_available (const gchar * name)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory) {
    gst_object_unref (factory);
    return TRUE;
  }

  return FALSE;
}

static gboolean
require_elements_or_skip (const gchar * const *elements, gsize n_elements)
{
  gboolean strict = g_getenv ("GST_REQUIRE_TEST_ELEMENTS") != NULL;
  gsize i;

  for (i = 0; i < n_elements; i++) {
    if (element_available (elements[i]))
      continue;
    if (strict)
      fail_unless (FALSE, "Missing required element: %s", elements[i]);
    GST_INFO ("Skipping test, missing required element: %s", elements[i]);
    return FALSE;
  }

  return TRUE;
}

static void
on_pad_added (GstElement * demux, GstPad * pad, gpointer user_data)
{
  DemuxCtx *ctx = user_data;
  GstPad *sinkpad;

  sinkpad = gst_element_get_static_pad (ctx->sink, "sink");
  if (!sinkpad)
    return;

  if (!gst_pad_is_linked (sinkpad))
    gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}

static void
run_pipeline (const gchar * launch)
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;

  pipeline = gst_parse_launch (launch, NULL);
  fail_unless (pipeline != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg != NULL);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *error = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &error, &debug);
    gst_object_default_error (GST_MESSAGE_SRC (msg), error, debug);
    g_error_free (error);
    g_free (debug);
    fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  }

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

static GstCaps *
run_demux_and_get_caps (const gchar * location)
{
  GstElement *pipeline;
  GstElement *demux;
  GstElement *sink;
  gchar *launch;
  DemuxCtx ctx = { 0, };
  GstSample *sample;
  GstCaps *caps;

  launch = g_strdup_printf ("filesrc location=%s ! matroskademux name=demux "
      "! queue ! appsink name=sink sync=false", location);
  pipeline = gst_parse_launch (launch, NULL);
  g_free (launch);
  fail_unless (pipeline != NULL);

  demux = gst_bin_get_by_name (GST_BIN (pipeline), "demux");
  fail_unless (demux != NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (sink != NULL);

  ctx.sink = sink;
  g_signal_connect (demux, "pad-added", G_CALLBACK (on_pad_added), &ctx);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_object_set (sink, "max-buffers", 1, "drop", TRUE, NULL);
  sample = gst_app_sink_try_pull_sample (GST_APP_SINK (sink), 5 * GST_SECOND);
  fail_unless (sample != NULL);
  caps = gst_sample_get_caps (sample);
  fail_unless (caps != NULL);
  caps = gst_caps_copy (caps);
  gst_sample_unref (sample);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (sink);
  gst_object_unref (demux);
  gst_object_unref (pipeline);
  return caps;
}

static void
test_mux_demux_vp9_codecprivate_roundtrip (const gchar * muxer_name,
    const gchar * file_ext)
{
  gchar *tmpdir;
  gchar *filepath;
  gchar *launch;
  GstCaps *caps;
  const GstStructure *s;
  const gchar *profile;
  const gchar *level;
  const gchar *chroma_format;
  const gchar *chroma_site;
  guint bitdepth_luma = 0, bitdepth_chroma = 0;
  const gchar *required[] = { "vp9enc", "capssetter", muxer_name,
    "matroskademux"
  };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  tmpdir = g_strdup_printf ("%s%sgst-vp9-codecprivate-XXXXXX",
      g_get_tmp_dir (), G_DIR_SEPARATOR_S);
  fail_unless (g_mkdtemp (tmpdir) != NULL);
  filepath = g_build_filename (tmpdir, file_ext, NULL);

  launch = g_strdup_printf ("videotestsrc num-buffers=5 ! "
      "video/x-raw,format=I420,width=320,height=180,framerate=15/1 ! "
      "vp9enc ! capssetter caps="
      "\"video/x-vp9,profile=(string)0,level=(string)4.1,"
      "bit-depth-luma=(uint)8,bit-depth-chroma=(uint)8,"
      "chroma-format=(string)4:2:0,chroma-site=(string)v-cosited\" ! "
      "%s ! filesink location=%s", muxer_name, filepath);
  run_pipeline (launch);
  g_free (launch);

  caps = run_demux_and_get_caps (filepath);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "video/x-vp9"));

  profile = gst_structure_get_string (s, "profile");
  level = gst_structure_get_string (s, "level");
  chroma_format = gst_structure_get_string (s, "chroma-format");
  chroma_site = gst_structure_get_string (s, "chroma-site");
  fail_unless (gst_structure_get_uint (s, "bit-depth-luma", &bitdepth_luma));
  fail_unless (gst_structure_get_uint (s, "bit-depth-chroma",
          &bitdepth_chroma));

  fail_unless_equals_string (profile, "0");
  fail_unless_equals_string (level, "4.1");
  fail_unless_equals_string (chroma_format, "4:2:0");
  fail_unless_equals_string (chroma_site, "v-cosited");
  fail_unless_equals_int (bitdepth_luma, 8);
  fail_unless_equals_int (bitdepth_chroma, 8);

  gst_caps_unref (caps);
  g_unlink (filepath);
  g_rmdir (tmpdir);
  g_free (filepath);
  g_free (tmpdir);
}

GST_START_TEST (test_matroskamux_matroskademux_vp9_codecprivate)
{
  test_mux_demux_vp9_codecprivate_roundtrip ("matroskamux", "roundtrip.mkv");
}

GST_END_TEST;

GST_START_TEST (test_webmmux_matroskademux_vp9_codecprivate)
{
  test_mux_demux_vp9_codecprivate_roundtrip ("webmmux", "roundtrip.webm");
}

GST_END_TEST;

static Suite *
vp9_codecprivate_suite (void)
{
  Suite *s = suite_create ("vp9-codecprivate");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_matroskamux_matroskademux_vp9_codecprivate);
  tcase_add_test (tc_chain, test_webmmux_matroskademux_vp9_codecprivate);

  return s;
}

GST_CHECK_MAIN (vp9_codecprivate);
