/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
 *
 * gstutils.c: Unit test for functions in gstutils
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

#define SPECIAL_POINTER(x) ((void*)(19283847+(x)))

static int n_data_probes = 0;
static int n_buffer_probes = 0;
static int n_event_probes = 0;

static GstPadProbeReturn
probe_do_nothing (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstMiniObject *obj = GST_PAD_PROBE_INFO_DATA (info);
  GST_DEBUG_OBJECT (pad, "is buffer:%d", GST_IS_BUFFER (obj));
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
data_probe (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstMiniObject *obj = GST_PAD_PROBE_INFO_DATA (info);
  n_data_probes++;
  GST_DEBUG_OBJECT (pad, "data probe %d", n_data_probes);
  g_assert (GST_IS_BUFFER (obj) || GST_IS_EVENT (obj));
  g_assert (data == SPECIAL_POINTER (0));
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
buffer_probe (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstBuffer *obj = GST_PAD_PROBE_INFO_BUFFER (info);
  n_buffer_probes++;
  GST_DEBUG_OBJECT (pad, "buffer probe %d", n_buffer_probes);
  g_assert (GST_IS_BUFFER (obj));
  g_assert (data == SPECIAL_POINTER (1));
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstEvent *obj = GST_PAD_PROBE_INFO_EVENT (info);
  n_event_probes++;
  GST_DEBUG_OBJECT (pad, "event probe %d [%s]",
      n_event_probes, GST_EVENT_TYPE_NAME (obj));
  g_assert (GST_IS_EVENT (obj));
  g_assert (data == SPECIAL_POINTER (2));
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_buffer_probe_n_times)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstBus *bus;
  GstMessage *message;
  GstPad *pad;

  pipeline = gst_element_factory_make ("pipeline", NULL);
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  g_object_set (fakesrc, "num-buffers", (int) 10, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  pad = gst_element_get_static_pad (fakesink, "sink");

  /* add the probes we need for the test */
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_DATA_BOTH, data_probe,
      SPECIAL_POINTER (0), NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, buffer_probe,
      SPECIAL_POINTER (1), NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_BOTH, event_probe,
      SPECIAL_POINTER (2), NULL);

  /* add some string probes just to test that the data is free'd
   * properly as it should be */
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_DATA_BOTH, probe_do_nothing,
      g_strdup ("data probe string"), (GDestroyNotify) g_free);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, probe_do_nothing,
      g_strdup ("buffer probe string"), (GDestroyNotify) g_free);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_BOTH, probe_do_nothing,
      g_strdup ("event probe string"), (GDestroyNotify) g_free);

  gst_object_unref (pad);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);
  message = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
  gst_message_unref (message);
  gst_object_unref (bus);

  g_assert (n_buffer_probes == 10);     /* one for every buffer */
  g_assert (n_event_probes == 4);       /* stream-start, new segment, latency and eos */
  g_assert (n_data_probes == 14);       /* duh */

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  /* make sure nothing was sent in addition to the above when shutting down */
  g_assert (n_buffer_probes == 10);     /* one for every buffer */
  g_assert (n_event_probes == 4);       /* stream-start, new segment, latency and eos */
  g_assert (n_data_probes == 14);       /* duh */
} GST_END_TEST;

static int n_data_probes_once = 0;
static int n_buffer_probes_once = 0;
static int n_event_probes_once = 0;

static GstPadProbeReturn
data_probe_once (GstPad * pad, GstPadProbeInfo * info, guint * data)
{
  GstMiniObject *obj = GST_PAD_PROBE_INFO_DATA (info);

  n_data_probes_once++;
  g_assert (GST_IS_BUFFER (obj) || GST_IS_EVENT (obj));

  gst_pad_remove_probe (pad, *data);

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
buffer_probe_once (GstPad * pad, GstPadProbeInfo * info, guint * data)
{
  GstBuffer *obj = GST_PAD_PROBE_INFO_BUFFER (info);

  n_buffer_probes_once++;
  g_assert (GST_IS_BUFFER (obj));

  gst_pad_remove_probe (pad, *data);

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
event_probe_once (GstPad * pad, GstPadProbeInfo * info, guint * data)
{
  GstEvent *obj = GST_PAD_PROBE_INFO_EVENT (info);

  n_event_probes_once++;
  g_assert (GST_IS_EVENT (obj));

  gst_pad_remove_probe (pad, *data);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_buffer_probe_once)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstBus *bus;
  GstMessage *message;
  GstPad *pad;
  guint id1, id2, id3;

  pipeline = gst_element_factory_make ("pipeline", NULL);
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  g_object_set (fakesrc, "num-buffers", (int) 10, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  pad = gst_element_get_static_pad (fakesink, "sink");
  id1 =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_DATA_BOTH,
      (GstPadProbeCallback) data_probe_once, &id1, NULL);
  id2 =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) buffer_probe_once, &id2, NULL);
  id3 =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) event_probe_once, &id3, NULL);
  gst_object_unref (pad);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);
  message = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
  gst_message_unref (message);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_assert (n_buffer_probes_once == 1); /* can we hit it and quit? */
  g_assert (n_event_probes_once == 1);  /* i said, can we hit it and quit? */
  g_assert (n_data_probes_once == 1);   /* let's hit it and quit!!! */
} GST_END_TEST;

GST_START_TEST (test_math_scale)
{
  fail_if (gst_util_uint64_scale_int (1, 1, 1) != 1);

  fail_if (gst_util_uint64_scale_int (10, 10, 1) != 100);
  fail_if (gst_util_uint64_scale_int (10, 10, 2) != 50);

  fail_if (gst_util_uint64_scale_int (0, 10, 2) != 0);
  fail_if (gst_util_uint64_scale_int (0, 0, 2) != 0);

  fail_if (gst_util_uint64_scale_int (G_MAXUINT32, 5, 1) != G_MAXUINT32 * 5LL);
  fail_if (gst_util_uint64_scale_int (G_MAXUINT32, 10, 2) != G_MAXUINT32 * 5LL);

  fail_if (gst_util_uint64_scale_int (G_MAXUINT32, 1, 5) != G_MAXUINT32 / 5LL);
  fail_if (gst_util_uint64_scale_int (G_MAXUINT32, 2, 10) != G_MAXUINT32 / 5LL);

  /* not quite overflow */
  fail_if (gst_util_uint64_scale_int (G_MAXUINT64 - 1, 10,
          10) != G_MAXUINT64 - 1);
  fail_if (gst_util_uint64_scale_int (G_MAXUINT64 - 1, G_MAXINT32,
          G_MAXINT32) != G_MAXUINT64 - 1);
  fail_if (gst_util_uint64_scale_int (G_MAXUINT64 - 100, G_MAXINT32,
          G_MAXINT32) != G_MAXUINT64 - 100);

  /* overflow */
  fail_if (gst_util_uint64_scale_int (G_MAXUINT64 - 1, 10, 1) != G_MAXUINT64);
  fail_if (gst_util_uint64_scale_int (G_MAXUINT64 - 1, G_MAXINT32,
          1) != G_MAXUINT64);

} GST_END_TEST;

GST_START_TEST (test_math_scale_round)
{
  fail_if (gst_util_uint64_scale_int_round (2, 1, 2) != 1);
  fail_if (gst_util_uint64_scale_int_round (3, 1, 2) != 2);
  fail_if (gst_util_uint64_scale_int_round (4, 1, 2) != 2);

  fail_if (gst_util_uint64_scale_int_round (200, 100, 20000) != 1);
  fail_if (gst_util_uint64_scale_int_round (299, 100, 20000) != 1);
  fail_if (gst_util_uint64_scale_int_round (300, 100, 20000) != 2);
  fail_if (gst_util_uint64_scale_int_round (301, 100, 20000) != 2);
  fail_if (gst_util_uint64_scale_int_round (400, 100, 20000) != 2);
} GST_END_TEST;

GST_START_TEST (test_math_scale_ceil)
{
  fail_if (gst_util_uint64_scale_int_ceil (2, 1, 2) != 1);
  fail_if (gst_util_uint64_scale_int_ceil (3, 1, 2) != 2);
  fail_if (gst_util_uint64_scale_int_ceil (4, 1, 2) != 2);

  fail_if (gst_util_uint64_scale_int_ceil (200, 100, 20000) != 1);
  fail_if (gst_util_uint64_scale_int_ceil (299, 100, 20000) != 2);
  fail_if (gst_util_uint64_scale_int_ceil (300, 100, 20000) != 2);
  fail_if (gst_util_uint64_scale_int_ceil (301, 100, 20000) != 2);
  fail_if (gst_util_uint64_scale_int_ceil (400, 100, 20000) != 2);
} GST_END_TEST;

GST_START_TEST (test_math_scale_uint64)
{
  fail_if (gst_util_uint64_scale (1, 1, 1) != 1);

  fail_if (gst_util_uint64_scale (10, 10, 1) != 100);
  fail_if (gst_util_uint64_scale (10, 10, 2) != 50);

  fail_if (gst_util_uint64_scale (0, 10, 2) != 0);
  fail_if (gst_util_uint64_scale (0, 0, 2) != 0);

  fail_if (gst_util_uint64_scale (G_MAXUINT32, 5, 1) != G_MAXUINT32 * 5LL);
  fail_if (gst_util_uint64_scale (G_MAXUINT32, 10, 2) != G_MAXUINT32 * 5LL);

  fail_if (gst_util_uint64_scale (G_MAXUINT32, 1, 5) != G_MAXUINT32 / 5LL);
  fail_if (gst_util_uint64_scale (G_MAXUINT32, 2, 10) != G_MAXUINT32 / 5LL);

  /* not quite overflow */
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 1, 10, 10) != G_MAXUINT64 - 1);
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 1, G_MAXUINT32,
          G_MAXUINT32) != G_MAXUINT64 - 1);
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 100, G_MAXUINT32,
          G_MAXUINT32) != G_MAXUINT64 - 100);

  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 1, 10, 10) != G_MAXUINT64 - 1);
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 1, G_MAXUINT64,
          G_MAXUINT64) != G_MAXUINT64 - 1);
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 100, G_MAXUINT64,
          G_MAXUINT64) != G_MAXUINT64 - 100);

  /* overflow */
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 1, 10, 1) != G_MAXUINT64);
  fail_if (gst_util_uint64_scale (G_MAXUINT64 - 1, G_MAXUINT64,
          1) != G_MAXUINT64);

} GST_END_TEST;

GST_START_TEST (test_math_scale_random)
{
  guint64 val, num, denom, res;
  GRand *rand;
  gint i;

  rand = g_rand_new ();

  i = 100000;
  while (i--) {
    guint64 check, diff;

    val = ((guint64) g_rand_int (rand)) << 32 | g_rand_int (rand);
    num = ((guint64) g_rand_int (rand)) << 32 | g_rand_int (rand);
    denom = ((guint64) g_rand_int (rand)) << 32 | g_rand_int (rand);

    res = gst_util_uint64_scale (val, num, denom);
    check = gst_gdouble_to_guint64 (gst_guint64_to_gdouble (val) *
        gst_guint64_to_gdouble (num) / gst_guint64_to_gdouble (denom));

    if (res < G_MAXUINT64 && check < G_MAXUINT64) {
      if (res > check)
        diff = res - check;
      else
        diff = check - res;

      /* some arbitrary value, really.. someone do the proper math to get
       * the upper bound */
      if (diff > 20000)
        fail_if (diff > 20000);
    }
  }
  g_rand_free (rand);

}

GST_END_TEST;

GST_START_TEST (test_guint64_to_gdouble)
{
  guint64 from[] = { 0, 1, 100, 10000, (guint64) (1) << 63,
    ((guint64) (1) << 63) + 1,
    ((guint64) (1) << 63) + (G_GINT64_CONSTANT (1) << 62)
  };
  gdouble to[] = { 0., 1., 100., 10000., 9223372036854775808.,
    9223372036854775809., 13835058055282163712.
  };
  gdouble tolerance[] = { 0., 0., 0., 0., 0., 1., 1. };
  gint i;
  gdouble result;
  gdouble delta;

  for (i = 0; i < G_N_ELEMENTS (from); ++i) {
    result = gst_util_guint64_to_gdouble (from[i]);
    delta = ABS (to[i] - result);
    fail_unless (delta <= tolerance[i],
        "Could not convert %d: %" G_GUINT64_FORMAT
        " -> %f, got %f instead, delta of %e with tolerance of %e",
        i, from[i], to[i], result, delta, tolerance[i]);
  }
}

GST_END_TEST;

GST_START_TEST (test_gdouble_to_guint64)
{
  gdouble from[] = { 0., 1., 100., 10000., 9223372036854775808.,
    9223372036854775809., 13835058055282163712.
  };
  guint64 to[] = { 0, 1, 100, 10000, (guint64) (1) << 63,
    ((guint64) (1) << 63) + 1,
    ((guint64) (1) << 63) + (G_GINT64_CONSTANT (1) << 62)
  };
  guint64 tolerance[] = { 0, 0, 0, 0, 0, 1, 1 };
  gint i;
  gdouble result;
  guint64 delta;

  for (i = 0; i < G_N_ELEMENTS (from); ++i) {
    result = gst_util_gdouble_to_guint64 (from[i]);
    delta = ABS (to[i] - result);
    fail_unless (delta <= tolerance[i],
        "Could not convert %f: %" G_GUINT64_FORMAT
        " -> %d, got %d instead, delta of %e with tolerance of %e",
        i, from[i], to[i], result, delta, tolerance[i]);
  }
}

GST_END_TEST;

#ifndef GST_DISABLE_PARSE
GST_START_TEST (test_parse_bin_from_description)
{
  struct
  {
    const gchar *bin_desc;
    const gchar *pad_names;
  } bin_tests[] = {
    {
    "identity", "identity0/sink,identity0/src"}, {
    "identity ! identity ! identity", "identity1/sink,identity3/src"}, {
    "identity ! fakesink", "identity4/sink"}, {
    "fakesrc ! identity", "identity5/src"}, {
    "fakesrc ! fakesink", ""}
  };
  gint i;

  for (i = 0; i < G_N_ELEMENTS (bin_tests); ++i) {
    GstElement *bin, *parent;
    GString *s;
    GstPad *ghost_pad, *target_pad;
    GError *err = NULL;

    bin = gst_parse_bin_from_description (bin_tests[i].bin_desc, TRUE, &err);
    if (err) {
      g_error ("ERROR in gst_parse_bin_from_description (%s): %s",
          bin_tests[i].bin_desc, err->message);
    }
    g_assert (bin != NULL);

    s = g_string_new ("");
    if ((ghost_pad = gst_element_get_static_pad (bin, "sink"))) {
      g_assert (GST_IS_GHOST_PAD (ghost_pad));

      target_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (ghost_pad));
      g_assert (target_pad != NULL);
      g_assert (GST_IS_PAD (target_pad));

      parent = gst_pad_get_parent_element (target_pad);
      g_assert (parent != NULL);

      g_string_append_printf (s, "%s/sink", GST_ELEMENT_NAME (parent));

      gst_object_unref (parent);
      gst_object_unref (target_pad);
      gst_object_unref (ghost_pad);
    }

    if ((ghost_pad = gst_element_get_static_pad (bin, "src"))) {
      g_assert (GST_IS_GHOST_PAD (ghost_pad));

      target_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (ghost_pad));
      g_assert (target_pad != NULL);
      g_assert (GST_IS_PAD (target_pad));

      parent = gst_pad_get_parent_element (target_pad);
      g_assert (parent != NULL);

      if (s->len > 0) {
        g_string_append (s, ",");
      }

      g_string_append_printf (s, "%s/src", GST_ELEMENT_NAME (parent));

      gst_object_unref (parent);
      gst_object_unref (target_pad);
      gst_object_unref (ghost_pad);
    }

    if (strcmp (s->str, bin_tests[i].pad_names) != 0) {
      g_error ("FAILED: expected '%s', got '%s' for bin '%s'",
          bin_tests[i].pad_names, s->str, bin_tests[i].bin_desc);
    }
    g_string_free (s, TRUE);

    gst_object_unref (bin);
  }
}

GST_END_TEST;
#endif

GST_START_TEST (test_element_found_tags)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstTagList *list;
  GstBus *bus;
  GstMessage *message;
  GstPad *srcpad;

  pipeline = gst_element_factory_make ("pipeline", NULL);
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  list = gst_tag_list_new_empty ();

  g_object_set (fakesrc, "num-buffers", (int) 10, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  srcpad = gst_element_get_static_pad (fakesrc, "src");
  gst_pad_push_event (srcpad, gst_event_new_tag (list));
  gst_object_unref (srcpad);

  bus = gst_element_get_bus (pipeline);
  message = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
  gst_message_unref (message);
  gst_object_unref (bus);

  /* FIXME: maybe also check if the fakesink receives the message */

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_element_unlink)
{
  GstElement *src, *sink;

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (gst_element_link (src, sink) != FALSE);
  gst_element_unlink (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

GST_START_TEST (test_set_value_from_string)
{
  GValue val = { 0, };

  /* g_return_if_fail */
  ASSERT_CRITICAL (gst_util_set_value_from_string (NULL, "xyz"));

  g_value_init (&val, G_TYPE_STRING);
  ASSERT_CRITICAL (gst_util_set_value_from_string (&val, NULL));
  g_value_unset (&val);

  /* string => string */
  g_value_init (&val, G_TYPE_STRING);
  gst_util_set_value_from_string (&val, "Y00");
  fail_unless (g_value_get_string (&val) != NULL);
  fail_unless_equals_string (g_value_get_string (&val), "Y00");
  g_value_unset (&val);

  /* string => int */
  g_value_init (&val, G_TYPE_INT);
  gst_util_set_value_from_string (&val, "987654321");
  fail_unless (g_value_get_int (&val) == 987654321);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_INT);
  ASSERT_CRITICAL (gst_util_set_value_from_string (&val, "xyz"));
  g_value_unset (&val);

  /* string => uint */
  g_value_init (&val, G_TYPE_UINT);
  gst_util_set_value_from_string (&val, "987654321");
  fail_unless (g_value_get_uint (&val) == 987654321);
  g_value_unset (&val);

  /* CHECKME: is this really desired behaviour? (tpm) */
  g_value_init (&val, G_TYPE_UINT);
  gst_util_set_value_from_string (&val, "-999");
  fail_unless (g_value_get_uint (&val) == ((guint) 0 - (guint) 999));
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_UINT);
  ASSERT_CRITICAL (gst_util_set_value_from_string (&val, "xyz"));
  g_value_unset (&val);

  /* string => long */
  g_value_init (&val, G_TYPE_LONG);
  gst_util_set_value_from_string (&val, "987654321");
  fail_unless (g_value_get_long (&val) == 987654321);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_LONG);
  ASSERT_CRITICAL (gst_util_set_value_from_string (&val, "xyz"));
  g_value_unset (&val);

  /* string => ulong */
  g_value_init (&val, G_TYPE_ULONG);
  gst_util_set_value_from_string (&val, "987654321");
  fail_unless (g_value_get_ulong (&val) == 987654321);
  g_value_unset (&val);

  /* CHECKME: is this really desired behaviour? (tpm) */
  g_value_init (&val, G_TYPE_ULONG);
  gst_util_set_value_from_string (&val, "-999");
  fail_unless (g_value_get_ulong (&val) == ((gulong) 0 - (gulong) 999));
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_ULONG);
  ASSERT_CRITICAL (gst_util_set_value_from_string (&val, "xyz"));
  g_value_unset (&val);

  /* string => boolean */
  g_value_init (&val, G_TYPE_BOOLEAN);
  gst_util_set_value_from_string (&val, "true");
  fail_unless_equals_int (g_value_get_boolean (&val), TRUE);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_BOOLEAN);
  gst_util_set_value_from_string (&val, "TRUE");
  fail_unless_equals_int (g_value_get_boolean (&val), TRUE);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_BOOLEAN);
  gst_util_set_value_from_string (&val, "false");
  fail_unless_equals_int (g_value_get_boolean (&val), FALSE);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_BOOLEAN);
  gst_util_set_value_from_string (&val, "FALSE");
  fail_unless_equals_int (g_value_get_boolean (&val), FALSE);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_BOOLEAN);
  gst_util_set_value_from_string (&val, "bleh");
  fail_unless_equals_int (g_value_get_boolean (&val), FALSE);
  g_value_unset (&val);

#if 0
  /* string => float (yay, localisation issues involved) */
  g_value_init (&val, G_TYPE_FLOAT);
  gst_util_set_value_from_string (&val, "987.654");
  fail_unless (g_value_get_float (&val) >= 987.653 &&
      g_value_get_float (&val) <= 987.655);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_FLOAT);
  gst_util_set_value_from_string (&val, "987,654");
  fail_unless (g_value_get_float (&val) >= 987.653 &&
      g_value_get_float (&val) <= 987.655);
  g_value_unset (&val);

  /* string => double (yay, localisation issues involved) */
  g_value_init (&val, G_TYPE_DOUBLE);
  gst_util_set_value_from_string (&val, "987.654");
  fail_unless (g_value_get_double (&val) >= 987.653 &&
      g_value_get_double (&val) <= 987.655);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_DOUBLE);
  gst_util_set_value_from_string (&val, "987,654");
  fail_unless (g_value_get_double (&val) >= 987.653 &&
      g_value_get_double (&val) <= 987.655);
  g_value_unset (&val);
#endif
}

GST_END_TEST;

static gint
_binary_search_compare (guint32 * a, guint32 * b)
{
  return *a - *b;
}

GST_START_TEST (test_binary_search)
{
  guint32 data[257];
  guint32 *match;
  guint32 search_element = 121 * 2;
  guint i;

  for (i = 0; i < 257; i++)
    data[i] = (i + 1) * 2;

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_EXACT,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 120);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_BEFORE,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 120);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_AFTER,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 120);

  search_element = 0;
  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_EXACT,
      &search_element, NULL);
  fail_unless (match == NULL);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_AFTER,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 0);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_BEFORE,
      &search_element, NULL);
  fail_unless (match == NULL);

  search_element = 1000;
  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_EXACT,
      &search_element, NULL);
  fail_unless (match == NULL);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_AFTER,
      &search_element, NULL);
  fail_unless (match == NULL);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_BEFORE,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 256);

  search_element = 121 * 2 - 1;
  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_EXACT,
      &search_element, NULL);
  fail_unless (match == NULL);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_AFTER,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 120);

  match =
      (guint32 *) gst_util_array_binary_search (data, 257, sizeof (guint32),
      (GCompareDataFunc) _binary_search_compare, GST_SEARCH_MODE_BEFORE,
      &search_element, NULL);
  fail_unless (match != NULL);
  fail_unless_equals_int (match - data, 119);

}

GST_END_TEST;

#ifdef HAVE_GSL
#ifdef HAVE_GMP

#include <gsl/gsl_rng.h>
#include <gmp.h>

static guint64
randguint64 (gsl_rng * rng, guint64 n)
{
  union
  {
    guint64 x;
    struct
    {
      guint16 a, b, c, d;
    } parts;
  } x;
  x.parts.a = gsl_rng_uniform_int (rng, 1 << 16);
  x.parts.b = gsl_rng_uniform_int (rng, 1 << 16);
  x.parts.c = gsl_rng_uniform_int (rng, 1 << 16);
  x.parts.d = gsl_rng_uniform_int (rng, 1 << 16);
  return x.x % n;
}


enum round_t
{
  ROUND_TONEAREST = 0,
  ROUND_UP,
  ROUND_DOWN
};

static void
gmp_set_uint64 (mpz_t mp, guint64 x)
{
  mpz_t two_32, tmp;

  mpz_init (two_32);
  mpz_init (tmp);

  mpz_ui_pow_ui (two_32, 2, 32);
  mpz_set_ui (mp, (unsigned long) ((x >> 32) & G_MAXUINT32));
  mpz_mul (tmp, mp, two_32);
  mpz_add_ui (mp, tmp, (unsigned long) (x & G_MAXUINT32));
  mpz_clear (two_32);
  mpz_clear (tmp);
}

static guint64
gmp_get_uint64 (mpz_t mp)
{
  mpz_t two_64, two_32, tmp;
  guint64 ret;

  mpz_init (two_64);
  mpz_init (two_32);
  mpz_init (tmp);

  mpz_ui_pow_ui (two_64, 2, 64);
  mpz_ui_pow_ui (two_32, 2, 32);
  if (mpz_cmp (tmp, two_64) >= 0)
    return G_MAXUINT64;
  mpz_clear (two_64);

  mpz_tdiv_q (tmp, mp, two_32);
  ret = mpz_get_ui (tmp);
  ret <<= 32;
  ret |= mpz_get_ui (mp);
  mpz_clear (two_32);
  mpz_clear (tmp);

  return ret;
}

static guint64
gmp_scale (guint64 x, guint64 a, guint64 b, enum round_t mode)
{
  mpz_t mp1, mp2, mp3;
  if (!b)
    /* overflow */
    return G_MAXUINT64;
  mpz_init (mp1);
  mpz_init (mp2);
  mpz_init (mp3);

  gmp_set_uint64 (mp1, x);
  gmp_set_uint64 (mp3, a);
  mpz_mul (mp2, mp1, mp3);
  switch (mode) {
    case ROUND_TONEAREST:
      gmp_set_uint64 (mp1, b);
      mpz_tdiv_q_ui (mp3, mp1, 2);
      mpz_add (mp1, mp2, mp3);
      mpz_set (mp2, mp1);
      break;
    case ROUND_UP:
      gmp_set_uint64 (mp1, b);
      mpz_sub_ui (mp3, mp1, 1);
      mpz_add (mp1, mp2, mp3);
      mpz_set (mp2, mp1);
      break;
    case ROUND_DOWN:
      break;
  }
  gmp_set_uint64 (mp3, b);
  mpz_tdiv_q (mp1, mp2, mp3);
  x = gmp_get_uint64 (mp1);
  mpz_clear (mp1);
  mpz_clear (mp2);
  mpz_clear (mp3);
  return x;
}

static void
_gmp_test_scale (gsl_rng * rng)
{
  guint64 bygst, bygmp;
  guint64 a = randguint64 (rng, gsl_rng_uniform_int (rng,
          2) ? G_MAXUINT64 : G_MAXUINT32);
  guint64 b = randguint64 (rng, gsl_rng_uniform_int (rng, 2) ? G_MAXUINT64 - 1 : G_MAXUINT32 - 1) + 1;  /* 0 not allowed */
  guint64 val = randguint64 (rng, gmp_scale (G_MAXUINT64, b, a, ROUND_DOWN));
  enum round_t mode = gsl_rng_uniform_int (rng, 3);
  const char *func;

  bygmp = gmp_scale (val, a, b, mode);
  switch (mode) {
    case ROUND_TONEAREST:
      bygst = gst_util_uint64_scale_round (val, a, b);
      func = "gst_util_uint64_scale_round";
      break;
    case ROUND_UP:
      bygst = gst_util_uint64_scale_ceil (val, a, b);
      func = "gst_util_uint64_scale_ceil";
      break;
    case ROUND_DOWN:
      bygst = gst_util_uint64_scale (val, a, b);
      func = "gst_util_uint64_scale";
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  fail_unless (bygst == bygmp,
      "error: %s(): %" G_GUINT64_FORMAT " * %" G_GUINT64_FORMAT " / %"
      G_GUINT64_FORMAT " = %" G_GUINT64_FORMAT ", correct = %" G_GUINT64_FORMAT
      "\n", func, val, a, b, bygst, bygmp);
}

static void
_gmp_test_scale_int (gsl_rng * rng)
{
  guint64 bygst, bygmp;
  gint32 a = randguint64 (rng, G_MAXINT32);
  gint32 b = randguint64 (rng, G_MAXINT32 - 1) + 1;     /* 0 not allowed */
  guint64 val = randguint64 (rng, gmp_scale (G_MAXUINT64, b, a, ROUND_DOWN));
  enum round_t mode = gsl_rng_uniform_int (rng, 3);
  const char *func;

  bygmp = gmp_scale (val, a, b, mode);
  switch (mode) {
    case ROUND_TONEAREST:
      bygst = gst_util_uint64_scale_int_round (val, a, b);
      func = "gst_util_uint64_scale_int_round";
      break;
    case ROUND_UP:
      bygst = gst_util_uint64_scale_int_ceil (val, a, b);
      func = "gst_util_uint64_scale_int_ceil";
      break;
    case ROUND_DOWN:
      bygst = gst_util_uint64_scale_int (val, a, b);
      func = "gst_util_uint64_scale_int";
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  fail_unless (bygst == bygmp,
      "error: %s(): %" G_GUINT64_FORMAT " * %d / %d = %" G_GUINT64_FORMAT
      ", correct = %" G_GUINT64_FORMAT "\n", func, val, a, b, bygst, bygmp);
}

#define GMP_TEST_RUNS 100000

GST_START_TEST (test_math_scale_gmp)
{
  gsl_rng *rng = gsl_rng_alloc (gsl_rng_mt19937);
  gint n;

  for (n = 0; n < GMP_TEST_RUNS; n++)
    _gmp_test_scale (rng);

  gsl_rng_free (rng);
}

GST_END_TEST;

GST_START_TEST (test_math_scale_gmp_int)
{
  gsl_rng *rng = gsl_rng_alloc (gsl_rng_mt19937);
  gint n;

  for (n = 0; n < GMP_TEST_RUNS; n++)
    _gmp_test_scale_int (rng);

  gsl_rng_free (rng);
}

GST_END_TEST;

#endif
#endif

GST_START_TEST (test_pad_proxy_query_caps_aggregation)
{
  GstElement *tee, *sink1, *sink2;
  GstCaps *caps;
  GstPad *tee_src1, *tee_src2, *tee_sink, *sink1_sink, *sink2_sink;

  tee = gst_element_factory_make ("tee", "tee");

  sink1 = gst_element_factory_make ("fakesink", "sink1");
  tee_src1 = gst_element_get_request_pad (tee, "src_%u");
  sink1_sink = gst_element_get_static_pad (sink1, "sink");
  fail_unless_equals_int (gst_pad_link (tee_src1, sink1_sink), GST_PAD_LINK_OK);

  sink2 = gst_element_factory_make ("fakesink", "sink2");
  tee_src2 = gst_element_get_request_pad (tee, "src_%u");
  sink2_sink = gst_element_get_static_pad (sink2, "sink");
  fail_unless_equals_int (gst_pad_link (tee_src2, sink2_sink), GST_PAD_LINK_OK);

  tee_sink = gst_element_get_static_pad (tee, "sink");

  gst_element_set_state (sink1, GST_STATE_PAUSED);
  gst_element_set_state (sink2, GST_STATE_PAUSED);
  gst_element_set_state (tee, GST_STATE_PAUSED);

  /* by default, ANY caps should intersect to ANY */
  caps = gst_pad_query_caps (tee_sink, NULL);
  GST_INFO ("got caps: %" GST_PTR_FORMAT, caps);
  fail_unless (caps != NULL);
  fail_unless (gst_caps_is_any (caps));
  gst_caps_unref (caps);

  /* these don't intersect we should get empty caps */
  caps = gst_caps_new_empty_simple ("foo/bar");
  fail_unless (gst_pad_set_caps (sink1_sink, caps));
  gst_pad_use_fixed_caps (sink1_sink);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty_simple ("bar/ter");
  fail_unless (gst_pad_set_caps (sink2_sink, caps));
  gst_pad_use_fixed_caps (sink2_sink);
  gst_caps_unref (caps);

  caps = gst_pad_query_caps (tee_sink, NULL);
  GST_INFO ("got caps: %" GST_PTR_FORMAT, caps);
  fail_unless (caps != NULL);
  fail_unless (gst_caps_is_empty (caps));
  gst_caps_unref (caps);

  /* test intersection */
  caps = gst_caps_new_simple ("foo/bar", "barversion", G_TYPE_INT, 1, NULL);
  GST_OBJECT_FLAG_UNSET (sink2_sink, GST_PAD_FLAG_FIXED_CAPS);
  fail_unless (gst_pad_set_caps (sink2_sink, caps));
  gst_pad_use_fixed_caps (sink2_sink);
  gst_caps_unref (caps);

  caps = gst_pad_query_caps (tee_sink, NULL);
  GST_INFO ("got caps: %" GST_PTR_FORMAT, caps);
  fail_unless (caps != NULL);
  fail_if (gst_caps_is_empty (caps));
  {
    GstStructure *s = gst_caps_get_structure (caps, 0);

    fail_unless_equals_string (gst_structure_get_name (s), "foo/bar");
    fail_unless (gst_structure_has_field_typed (s, "barversion", G_TYPE_INT));
  }
  gst_caps_unref (caps);

  gst_element_set_state (sink1, GST_STATE_NULL);
  gst_element_set_state (sink2, GST_STATE_NULL);
  gst_element_set_state (tee, GST_STATE_NULL);

  /* clean up */
  gst_element_release_request_pad (tee, tee_src1);
  gst_object_unref (tee_src1);
  gst_element_release_request_pad (tee, tee_src2);
  gst_object_unref (tee_src2);
  gst_object_unref (tee_sink);
  gst_object_unref (tee);
  gst_object_unref (sink1_sink);
  gst_object_unref (sink1);
  gst_object_unref (sink2_sink);
  gst_object_unref (sink2);
}

GST_END_TEST;

GST_START_TEST (test_greatest_common_divisor)
{
  fail_if (gst_util_greatest_common_divisor (1, 1) != 1);
  fail_if (gst_util_greatest_common_divisor (2, 3) != 1);
  fail_if (gst_util_greatest_common_divisor (3, 5) != 1);
  fail_if (gst_util_greatest_common_divisor (-1, 1) != 1);
  fail_if (gst_util_greatest_common_divisor (-2, 3) != 1);
  fail_if (gst_util_greatest_common_divisor (-3, 5) != 1);
  fail_if (gst_util_greatest_common_divisor (-1, -1) != 1);
  fail_if (gst_util_greatest_common_divisor (-2, -3) != 1);
  fail_if (gst_util_greatest_common_divisor (-3, -5) != 1);
  fail_if (gst_util_greatest_common_divisor (1, -1) != 1);
  fail_if (gst_util_greatest_common_divisor (2, -3) != 1);
  fail_if (gst_util_greatest_common_divisor (3, -5) != 1);
  fail_if (gst_util_greatest_common_divisor (2, 2) != 2);
  fail_if (gst_util_greatest_common_divisor (2, 4) != 2);
  fail_if (gst_util_greatest_common_divisor (1001, 11) != 11);

}

GST_END_TEST;

GST_START_TEST (test_read_macros)
{
  guint8 carray[] = "ABCDEFGH"; /* 0x41 ... 0x48 */
  guint32 uarray[2];
  guint8 *cpointer;

  memcpy (uarray, carray, 8);
  cpointer = carray;

  /* 16 bit */
  /* First try the standard pointer variants */
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer), 0x4142);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer + 1), 0x4243);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer + 2), 0x4344);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer + 3), 0x4445);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer + 4), 0x4546);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer + 5), 0x4647);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer + 6), 0x4748);

  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer), 0x4241);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer + 1), 0x4342);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer + 2), 0x4443);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer + 3), 0x4544);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer + 4), 0x4645);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer + 5), 0x4746);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (cpointer + 6), 0x4847);

  /* On an array of guint8 */
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray), 0x4142);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray + 1), 0x4243);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray + 2), 0x4344);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray + 3), 0x4445);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray + 4), 0x4546);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray + 5), 0x4647);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (carray + 6), 0x4748);

  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray), 0x4241);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray + 1), 0x4342);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray + 2), 0x4443);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray + 3), 0x4544);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray + 4), 0x4645);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray + 5), 0x4746);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (carray + 6), 0x4847);

  /* On an array of guint32 */
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (uarray), 0x4142);
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (uarray + 1), 0x4546);

  fail_unless_equals_int_hex (GST_READ_UINT16_LE (uarray), 0x4241);
  fail_unless_equals_int_hex (GST_READ_UINT16_LE (uarray + 1), 0x4645);


  /* 24bit */
  /* First try the standard pointer variants */
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (cpointer), 0x414243);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (cpointer + 1), 0x424344);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (cpointer + 2), 0x434445);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (cpointer + 3), 0x444546);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (cpointer + 4), 0x454647);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (cpointer + 5), 0x464748);

  fail_unless_equals_int_hex (GST_READ_UINT24_LE (cpointer), 0x434241);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (cpointer + 1), 0x444342);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (cpointer + 2), 0x454443);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (cpointer + 3), 0x464544);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (cpointer + 4), 0x474645);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (cpointer + 5), 0x484746);

  /* On an array of guint8 */
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (carray), 0x414243);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (carray + 1), 0x424344);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (carray + 2), 0x434445);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (carray + 3), 0x444546);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (carray + 4), 0x454647);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (carray + 5), 0x464748);

  fail_unless_equals_int_hex (GST_READ_UINT24_LE (carray), 0x434241);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (carray + 1), 0x444342);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (carray + 2), 0x454443);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (carray + 3), 0x464544);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (carray + 4), 0x474645);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (carray + 5), 0x484746);

  /* On an array of guint32 */
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (uarray), 0x414243);
  fail_unless_equals_int_hex (GST_READ_UINT24_BE (uarray + 1), 0x454647);

  fail_unless_equals_int_hex (GST_READ_UINT24_LE (uarray), 0x434241);
  fail_unless_equals_int_hex (GST_READ_UINT24_LE (uarray + 1), 0x474645);


  /* 32bit */
  /* First try the standard pointer variants */
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (cpointer), 0x41424344);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (cpointer + 1), 0x42434445);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (cpointer + 2), 0x43444546);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (cpointer + 3), 0x44454647);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (cpointer + 4), 0x45464748);

  fail_unless_equals_int_hex (GST_READ_UINT32_LE (cpointer), 0x44434241);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (cpointer + 1), 0x45444342);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (cpointer + 2), 0x46454443);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (cpointer + 3), 0x47464544);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (cpointer + 4), 0x48474645);

  /* On an array of guint8 */
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (carray), 0x41424344);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (carray + 1), 0x42434445);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (carray + 2), 0x43444546);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (carray + 3), 0x44454647);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (carray + 4), 0x45464748);

  fail_unless_equals_int_hex (GST_READ_UINT32_LE (carray), 0x44434241);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (carray + 1), 0x45444342);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (carray + 2), 0x46454443);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (carray + 3), 0x47464544);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (carray + 4), 0x48474645);

  /* On an array of guint32 */
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (uarray), 0x41424344);
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (uarray + 1), 0x45464748);

  fail_unless_equals_int_hex (GST_READ_UINT32_LE (uarray), 0x44434241);
  fail_unless_equals_int_hex (GST_READ_UINT32_LE (uarray + 1), 0x48474645);


  /* 64bit */
  fail_unless_equals_int64_hex (GST_READ_UINT64_BE (cpointer),
      0x4142434445464748);
  fail_unless_equals_int64_hex (GST_READ_UINT64_LE (cpointer),
      0x4847464544434241);

  fail_unless_equals_int64_hex (GST_READ_UINT64_BE (carray),
      0x4142434445464748);
  fail_unless_equals_int64_hex (GST_READ_UINT64_LE (carray),
      0x4847464544434241);

  fail_unless_equals_int64_hex (GST_READ_UINT64_BE (uarray),
      0x4142434445464748);
  fail_unless_equals_int64_hex (GST_READ_UINT64_LE (uarray),
      0x4847464544434241);

  /* make sure the data argument is not duplicated inside the macro
   * with possibly unexpected side-effects */
  cpointer = carray;
  fail_unless_equals_int (GST_READ_UINT8 (cpointer++), 'A');
  fail_unless (cpointer == carray + 1);

  cpointer = carray;
  fail_unless_equals_int_hex (GST_READ_UINT16_BE (cpointer++), 0x4142);
  fail_unless (cpointer == carray + 1);

  cpointer = carray;
  fail_unless_equals_int_hex (GST_READ_UINT32_BE (cpointer++), 0x41424344);
  fail_unless (cpointer == carray + 1);

  cpointer = carray;
  fail_unless_equals_int64_hex (GST_READ_UINT64_BE (cpointer++),
      0x4142434445464748);
  fail_unless (cpointer == carray + 1);
}

GST_END_TEST;

GST_START_TEST (test_write_macros)
{
  guint8 carray[8];
  guint8 *cpointer;

  /* make sure the data argument is not duplicated inside the macro
   * with possibly unexpected side-effects */
  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT8 (cpointer++, 'A');
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'A');

  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT16_BE (cpointer++, 0x4142);
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'A');
  fail_unless_equals_int (carray[1], 'B');

  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT32_BE (cpointer++, 0x41424344);
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'A');
  fail_unless_equals_int (carray[3], 'D');

  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT64_BE (cpointer++, 0x4142434445464748);
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'A');
  fail_unless_equals_int (carray[7], 'H');

  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT16_LE (cpointer++, 0x4142);
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'B');
  fail_unless_equals_int (carray[1], 'A');

  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT32_LE (cpointer++, 0x41424344);
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'D');
  fail_unless_equals_int (carray[3], 'A');

  memset (carray, 0, sizeof (carray));
  cpointer = carray;
  GST_WRITE_UINT64_LE (cpointer++, 0x4142434445464748);
  fail_unless_equals_pointer (cpointer, carray + 1);
  fail_unless_equals_int (carray[0], 'H');
  fail_unless_equals_int (carray[7], 'A');
}

GST_END_TEST;

static void
count_request_pad (const GValue * item, gpointer user_data)
{
  GstPad *pad = GST_PAD (g_value_get_object (item));
  guint *count = (guint *) user_data;

  if (GST_PAD_TEMPLATE_PRESENCE (GST_PAD_PAD_TEMPLATE (pad)) == GST_PAD_REQUEST)
    (*count)++;
}

static guint
request_pads (GstElement * element)
{
  GstIterator *iter;
  guint pads = 0;

  iter = gst_element_iterate_pads (element);
  fail_unless (gst_iterator_foreach (iter, count_request_pad, &pads) ==
      GST_ITERATOR_DONE);
  gst_iterator_free (iter);

  return pads;
}

static GstPadLinkReturn
refuse_to_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  return GST_PAD_LINK_REFUSED;
}

typedef struct _GstFakeReqSink GstFakeReqSink;
typedef struct _GstFakeReqSinkClass GstFakeReqSinkClass;

struct _GstFakeReqSink
{
  GstElement element;
};

struct _GstFakeReqSinkClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_fakereqsink_get_type (void);

static GstStaticPadTemplate fakereqsink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstFakeReqSink, gst_fakereqsink, GST_TYPE_ELEMENT);

static GstPad *
gst_fakereqsink_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *pad;
  pad = gst_pad_new_from_static_template (&fakereqsink_sink_template, name);
  gst_pad_set_link_function (pad, refuse_to_link);
  gst_element_add_pad (GST_ELEMENT_CAST (element), pad);
  return pad;
}

static void
gst_fakereqsink_release_pad (GstElement * element, GstPad * pad)
{
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
}

static void
gst_fakereqsink_class_init (GstFakeReqSinkClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (gstelement_class,
      "Fake Request Sink", "Sink", "Fake sink with request pads",
      "Sebastian Rasmussen <sebras@hotmail.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &fakereqsink_sink_template);

  gstelement_class->request_new_pad = gst_fakereqsink_request_new_pad;
  gstelement_class->release_pad = gst_fakereqsink_release_pad;
}

static void
gst_fakereqsink_init (GstFakeReqSink * fakereqsink)
{
}

static void
test_link (const gchar * expectation, const gchar * srcname,
    const gchar * srcpad, const gchar * srcstate, const gchar * sinkname,
    const gchar * sinkpad, const gchar * sinkstate)
{
  GstElement *src, *sink, *othersrc, *othersink;
  guint src_pads, sink_pads;

  if (g_strcmp0 (srcname, "requestsrc") == 0)
    src = gst_element_factory_make ("tee", NULL);
  else if (g_strcmp0 (srcname, "requestsink") == 0)
    src = gst_element_factory_make ("funnel", NULL);
  else if (g_strcmp0 (srcname, "staticsrc") == 0)
    src = gst_element_factory_make ("fakesrc", NULL);
  else if (g_strcmp0 (srcname, "staticsink") == 0)
    src = gst_element_factory_make ("fakesink", NULL);
  else
    g_assert_not_reached ();

  if (g_strcmp0 (sinkname, "requestsink") == 0)
    sink = gst_element_factory_make ("funnel", NULL);
  else if (g_strcmp0 (sinkname, "requestsrc") == 0)
    sink = gst_element_factory_make ("tee", NULL);
  else if (g_strcmp0 (sinkname, "staticsink") == 0)
    sink = gst_element_factory_make ("fakesink", NULL);
  else if (g_strcmp0 (sinkname, "staticsrc") == 0)
    sink = gst_element_factory_make ("fakesrc", NULL);
  else if (g_strcmp0 (sinkname, "fakerequestsink") == 0)
    sink = gst_element_factory_make ("fakereqsink", NULL);
  else
    g_assert_not_reached ();

  othersrc = gst_element_factory_make ("fakesrc", NULL);
  othersink = gst_element_factory_make ("fakesink", NULL);

  if (g_strcmp0 (srcstate, "linked") == 0)
    fail_unless (gst_element_link_pads (src, srcpad, othersink, NULL));
  if (g_strcmp0 (sinkstate, "linked") == 0)
    fail_unless (gst_element_link_pads (othersrc, NULL, sink, sinkpad));
  if (g_strcmp0 (srcstate, "unlinkable") == 0) {
    GstPad *pad = gst_element_get_static_pad (src, srcpad ? srcpad : "src");
    gst_pad_set_link_function (pad, refuse_to_link);
    gst_object_unref (pad);
  }
  if (g_strcmp0 (sinkstate, "unlinkable") == 0) {
    GstPad *pad = gst_element_get_static_pad (sink, sinkpad ? sinkpad : "sink");
    gst_pad_set_link_function (pad, refuse_to_link);
    gst_object_unref (pad);
  }

  src_pads = request_pads (src);
  sink_pads = request_pads (sink);
  if (g_strcmp0 (expectation, "OK") == 0) {
    fail_unless (gst_element_link_pads (src, srcpad, sink, sinkpad));
    if (g_str_has_prefix (srcname, "request")) {
      fail_unless_equals_int (request_pads (src), src_pads + 1);
    } else {
      fail_unless_equals_int (request_pads (src), src_pads);
    }
    if (g_str_has_prefix (sinkname, "request")) {
      fail_unless_equals_int (request_pads (sink), sink_pads + 1);
    } else {
      fail_unless_equals_int (request_pads (sink), sink_pads);
    }
  } else {
    fail_if (gst_element_link_pads (src, srcpad, sink, sinkpad));
    fail_unless_equals_int (request_pads (src), src_pads);
    fail_unless_equals_int (request_pads (sink), sink_pads);
  }

  gst_object_unref (othersrc);
  gst_object_unref (othersink);

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_START_TEST (test_element_link)
{
  /* Successful cases */

  gst_element_register (NULL, "fakereqsink", GST_RANK_NONE,
      gst_fakereqsink_get_type ());

  test_link ("OK", "staticsrc", "src", "", "staticsink", "sink", "");
  test_link ("OK", "staticsrc", "src", "", "requestsink", "sink_0", "");
  test_link ("OK", "staticsrc", "src", "", "staticsink", NULL, "");
  test_link ("OK", "staticsrc", "src", "", "requestsink", NULL, "");
  test_link ("OK", "requestsrc", "src_0", "", "staticsink", "sink", "");
  test_link ("OK", "requestsrc", "src_0", "", "requestsink", "sink_0", "");
  test_link ("OK", "requestsrc", "src_0", "", "staticsink", NULL, "");
  test_link ("OK", "requestsrc", "src_0", "", "requestsink", NULL, "");
  test_link ("OK", "staticsrc", NULL, "", "staticsink", "sink", "");
  test_link ("OK", "staticsrc", NULL, "", "requestsink", "sink_0", "");
  test_link ("OK", "staticsrc", NULL, "", "staticsink", NULL, "");
  test_link ("OK", "staticsrc", NULL, "", "requestsink", NULL, "");
  test_link ("OK", "requestsrc", NULL, "", "staticsink", "sink", "");
  test_link ("OK", "requestsrc", NULL, "", "requestsink", "sink_0", "");
  test_link ("OK", "requestsrc", NULL, "", "staticsink", NULL, "");
  test_link ("OK", "requestsrc", NULL, "", "requestsink", NULL, "");

  /* Failure cases */

  test_link ("NOK", "staticsrc", "missing", "", "staticsink", "sink", "");
  test_link ("NOK", "staticsink", "sink", "", "staticsink", "sink", "");
  test_link ("NOK", "staticsrc", "src", "linked", "staticsink", "sink", "");
  test_link ("NOK", "staticsrc", "src", "", "staticsink", "missing", "");
  test_link ("NOK", "staticsrc", "src", "", "staticsrc", "src", "");
  test_link ("NOK", "staticsrc", "src", "", "staticsink", "sink", "linked");
  test_link ("NOK", "staticsrc", "src", "", "staticsink", "sink", "unlinkable");
  test_link ("NOK", "staticsrc", NULL, "", "staticsink", "sink", "unlinkable");
  test_link ("NOK", "staticsrc", NULL, "", "staticsink", NULL, "unlinkable");
  test_link ("NOK", "requestsrc", "missing", "", "staticsink", "sink", "");
  test_link ("NOK", "requestsink", "sink_0", "", "staticsink", "sink", "");
  test_link ("NOK", "requestsrc", "src_0", "linked", "staticsink", "sink", "");
  test_link ("NOK", "requestsrc", "src_0", "", "staticsink", "missing", "");
  test_link ("NOK", "requestsrc", "src_0", "", "staticsrc", "src", "");
  test_link ("NOK", "requestsrc", "src_0", "", "staticsink", "sink", "linked");
  test_link ("NOK", "requestsrc", "src_0", "", "staticsink", "sink",
      "unlinkable");
  test_link ("NOK", "requestsrc", NULL, "", "staticsink", "sink", "unlinkable");
  test_link ("NOK", "requestsrc", NULL, "", "staticsink", NULL, "unlinkable");
  test_link ("NOK", "staticsrc", "missing", "", "requestsink", "sink_0", "");
  test_link ("NOK", "staticsink", "sink", "", "requestsink", "sink_0", "");
  test_link ("NOK", "staticsrc", "src", "linked", "requestsink", "sink_0", "");
  test_link ("NOK", "staticsrc", "src", "", "requestsink", "missing", "");
  test_link ("NOK", "staticsrc", "src", "", "requestsrc", "src_0", "");
  test_link ("NOK", "staticsrc", "src", "", "requestsink", "sink_0", "linked");
  test_link ("NOK", "staticsrc", "src", "unlinkable", "requestsink",
      "sink_0", "");
  test_link ("NOK", "staticsrc", NULL, "unlinkable", "requestsink",
      "sink_0", "");
  test_link ("NOK", "staticsrc", NULL, "unlinkable", "requestsink", NULL, "");
  test_link ("NOK", "requestsrc", "src_0", "", "staticsink", NULL,
      "unlinkable");
  test_link ("NOK", "requestsrc", NULL, "", "fakerequestsink", NULL, "");
}

GST_END_TEST;

typedef struct _GstTestPadReqSink GstTestPadReqSink;
typedef struct _GstTestPadReqSinkClass GstTestPadReqSinkClass;

struct _GstTestPadReqSink
{
  GstElement element;
};

struct _GstTestPadReqSinkClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_testpadreqsink_get_type (void);

static GstStaticPadTemplate testpadreqsink_video_template =
GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw"));

static GstStaticPadTemplate testpadreqsink_audio_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw"));

G_DEFINE_TYPE (GstTestPadReqSink, gst_testpadreqsink, GST_TYPE_ELEMENT);

static GstPad *
gst_testpadreqsink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstPad *pad;
  pad = gst_pad_new_from_template (templ, name);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (element), pad);
  return pad;
}

static void
gst_testpadreqsink_release_pad (GstElement * element, GstPad * pad)
{
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
}

static void
gst_testpadreqsink_class_init (GstTestPadReqSinkClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (gstelement_class,
      "Test Pad Request Sink", "Sink", "Sink for unit tests with request pads",
      "Thiago Santos <thiagoss@osg.samsung.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &testpadreqsink_video_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &testpadreqsink_audio_template);

  gstelement_class->request_new_pad = gst_testpadreqsink_request_new_pad;
  gstelement_class->release_pad = gst_testpadreqsink_release_pad;
}

static void
gst_testpadreqsink_init (GstTestPadReqSink * testpadeqsink)
{
}

static GstCaps *padreqsink_query_caps = NULL;

static gboolean
testpadreqsink_peer_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      if (padreqsink_query_caps) {
        gst_query_set_caps_result (query, padreqsink_query_caps);
        res = TRUE;
        break;
      }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static void
check_get_compatible_pad_request (GstElement * element, GstCaps * peer_caps,
    GstCaps * filter, gboolean should_get_pad, const gchar * pad_tmpl_name)
{
  GstPad *peer, *requested;
  GstPadTemplate *tmpl;

  gst_caps_replace (&padreqsink_query_caps, peer_caps);
  peer = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_query_function (peer, testpadreqsink_peer_query);
  requested = gst_element_get_compatible_pad (element, peer, filter);

  if (should_get_pad) {
    fail_unless (requested != NULL);
    if (pad_tmpl_name) {
      tmpl = gst_pad_get_pad_template (requested);
      fail_unless (strcmp (GST_PAD_TEMPLATE_NAME_TEMPLATE (tmpl),
              pad_tmpl_name) == 0);
      gst_object_unref (tmpl);
    }
    gst_element_release_request_pad (element, requested);
    gst_object_unref (requested);
  } else {
    fail_unless (requested == NULL);
  }

  if (peer_caps)
    gst_caps_unref (peer_caps);
  if (filter)
    gst_caps_unref (filter);
  gst_object_unref (peer);
}

GST_START_TEST (test_element_get_compatible_pad_request)
{
  GstElement *element;

  gst_element_register (NULL, "testpadreqsink", GST_RANK_NONE,
      gst_testpadreqsink_get_type ());

  element = gst_element_factory_make ("testpadreqsink", NULL);

  /* Try with a peer pad with any caps and no filter,
   * returning any pad is ok */
  check_get_compatible_pad_request (element, NULL, NULL, TRUE, NULL);
  /* Try with a peer pad with any caps and video as filter */
  check_get_compatible_pad_request (element, NULL,
      gst_caps_from_string ("video/x-raw"), TRUE, "video_%u");
  /* Try with a peer pad with any caps and audio as filter */
  check_get_compatible_pad_request (element, NULL,
      gst_caps_from_string ("audio/x-raw"), TRUE, "audio_%u");
  /* Try with a peer pad with any caps and fake caps as filter */
  check_get_compatible_pad_request (element, NULL,
      gst_caps_from_string ("foo/bar"), FALSE, NULL);

  /* Try with a peer pad with video caps and no caps as filter */
  check_get_compatible_pad_request (element,
      gst_caps_from_string ("video/x-raw"), NULL, TRUE, "video_%u");
  /* Try with a peer pad with audio caps and no caps as filter */
  check_get_compatible_pad_request (element,
      gst_caps_from_string ("audio/x-raw"), NULL, TRUE, "audio_%u");
  /* Try with a peer pad with video caps and foo caps as filter */
  check_get_compatible_pad_request (element,
      gst_caps_from_string ("video/x-raw"), gst_caps_from_string ("foo/bar"),
      FALSE, NULL);

  gst_caps_replace (&padreqsink_query_caps, NULL);
  gst_object_unref (element);
}

GST_END_TEST;

GST_START_TEST (test_element_link_with_ghost_pads)
{
  GstElement *sink_bin, *sink2_bin, *pipeline;
  GstElement *src, *tee, *queue, *queue2, *sink, *sink2;
  GstMessage *message;
  GstBus *bus;

  fail_unless (pipeline = gst_pipeline_new (NULL));
  fail_unless (sink_bin = gst_bin_new (NULL));
  fail_unless (sink2_bin = gst_bin_new (NULL));
  fail_unless (src = gst_element_factory_make ("fakesrc", NULL));
  fail_unless (tee = gst_element_factory_make ("tee", NULL));
  fail_unless (queue = gst_element_factory_make ("queue", NULL));
  fail_unless (sink = gst_element_factory_make ("fakesink", NULL));
  fail_unless (queue2 = gst_element_factory_make ("queue", NULL));
  fail_unless (sink2 = gst_element_factory_make ("fakesink", NULL));

  gst_bin_add_many (GST_BIN (pipeline), src, tee, queue, sink, sink2_bin, NULL);
  fail_unless (gst_element_link_many (src, tee, queue, sink, NULL));
  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_ASYNC);

  /* wait for a buffer to arrive at the sink */
  bus = gst_element_get_bus (pipeline);
  message = gst_bus_poll (bus, GST_MESSAGE_ASYNC_DONE, -1);
  gst_message_unref (message);
  gst_object_unref (bus);

  gst_bin_add_many (GST_BIN (sink_bin), queue2, sink2, NULL);
  fail_unless (gst_element_link (queue2, sink2));

  gst_bin_add (GST_BIN (sink2_bin), sink_bin);
  /* The two levels of bins with the outer bin in the running state is
   * important, when the second ghost pad is created (from this
   * gst_element_link()) in the running bin, we need to activate the
   * created ghost pad */
  fail_unless (gst_element_link (tee, queue2));

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (pipeline);
}

GST_END_TEST;

static const GstClockTime times1[] = {
  257116899087539, 120632754291904,
  257117935914250, 120633825367344,
  257119448289434, 120635306141271,
  257120493671524, 120636384357825,
  257121550784861, 120637417438878,
  257123042669403, 120638895344150,
  257124089184865, 120639971729651,
  257125545836474, 120641406788243,
  257127030618490, 120642885914220,
  257128033712770, 120643888843907,
  257129081768074, 120644981892002,
  257130145383845, 120646016376867,
  257131532530200, 120647389850987,
  257132578136034, 120648472767247,
  257134102475722, 120649953785315,
  257135142994788, 120651028858556,
  257136585079868, 120652441303624,
  257137618260656, 120653491627112,
  257139108694546, 120654963978184,
  257140644022048, 120656500233068,
  257141685671825, 120657578510655,
  257142741238288, 120658610889805,
  257144243633074, 120660093098060,
  257145287962271, 120661172901525,
  257146740596716, 120662591572179,
  257147757607150, 120663622822179,
  257149263992401, 120665135578527,
  257150303719290, 120666176166905,
  257151355569906, 120667217304601,
  257152430578406, 120668326099768,
  257153490501095, 120669360554111,
  257154512360784, 120670365497960,
  257155530610577, 120671399006259,
  257156562091659, 120672432728185,
  257157945388742, 120673800312414,
  257159287547073, 120675142444983,
  257160324912880, 120676215076817,
  257345408328042, 120861261738196,
  257346412270919, 120862265613926,
  257347420532284, 120863278644933,
  257348431187638, 120864284412754,
  257349439018028, 120865293110265,
  257351796217938, 120867651111973,
  257352803038092, 120868659107578,
  257354152688899, 120870008594883,
  257355157088906, 120871011097327,
  257356162439182, 120872016346348,
  257357167872040, 120873021656407,
  257358182440058, 120874048633945,
  257359198881356, 120875052265538,
  257100756525466, 120616619282139,
  257101789337770, 120617655475988,
  257102816323472, 120618674000157,
  257103822485250, 120619679005039,
  257104840760423, 120620710743321,
  257105859459496, 120621715351476,
  257106886662470, 120622764942539,
  257108387497864, 120624244221106,
  257109428859191, 120625321461096,
  257110485892785, 120626356892003,
  257111869872141, 120627726459874,
  257112915903774, 120628813190830,
  257114329982208, 120630187061682,
  257115376666026, 120631271992101
};


static const GstClockTime times2[] = {
  291678579009762, 162107345029507,
  291679770464405, 162108597684538,
  291680972924370, 162109745816863,
  291682278949629, 162111000577605,
  291683590706117, 162112357724822,
  291684792322541, 162113613156950,
  291685931362506, 162114760556854,
  291687132156589, 162115909238493,
  291688265012060, 162117120603240,
  291689372183047, 162118126279508,
  291705506022294, 162134329373992,
  291667914301004, 162096795553658,
  291668119537668, 162096949051905,
  291668274671455, 162097049238371,
  291668429435600, 162097256356719,
  291668586128535, 162097355689763,
  291668741306233, 162097565678460,
  291668893789203, 162097661044916,
  291669100256555, 162097865694145,
  291669216417563, 162098069214693,
  291669836394620, 162098677275530,
  291669990447821, 162098792601263,
  291670149426086, 162098916899184,
  291670300232152, 162099114225621,
  291670411261917, 162099236784112,
  291670598483507, 162099402158751,
  291671716582687, 162100558744122,
  291672600759788, 162101499326359,
  291673919988307, 162102751981384,
  291675174441643, 162104005551939,
  291676271562197, 162105105252898,
  291677376345374, 162106195737516
};

static const GstClockTime times3[] = {
  291881924291688, 162223997578228,
  291883318122262, 162224167198360,
  291884786394838, 162224335172501,
  291886004374386, 162224503695531,
  291887224353285, 162224673560021,
  291888472403367, 162224843760361,
  291889727977561, 162225014479362,
  291890989982306, 162225174554558,
  291892247875763, 162225339753039,
  291893502163547, 162225673230987,
  291894711382216, 162225829494101,
  291895961021506, 162225964530832,
  291897251690854, 162226127287981,
  291898508630785, 162226303710406,
  291899740172868, 162226472478047,
  291900998878873, 162226637402085,
  291902334919875, 162226797873245,
  291903572196610, 162226964352963,
  291904727342699, 162227125312525,
  291906071189108, 162228361337153,
  291907308146005, 162229560625638,
  291908351925126, 162230604986650,
  291909396411423, 162231653690543,
  291910453965348, 162232698550995,
  291912096870744, 162233475264947,
  291913234148395, 162233606516855,
  291915448096576, 162233921145559,
  291916707748827, 162234047154298,
  291918737451070, 162234370837425,
  291919896016205, 162234705504337,
  291921098663980, 162234872320397,
  291922315691409, 162235031023366
};

static const GstClockTime times4[] = {
  10, 0,
  20, 20,
  30, 40,
  40, 60,
  50, 80,
  60, 100
};

struct test_entry
{
  gint n;
  const GstClockTime *v;
  GstClockTime expect_internal;
  GstClockTime expect_external;
  guint64 expect_num;
  guint64 expect_denom;
} times[] = {
  {
  32, times1, 257154512360784, 120670380469753, 4052622913376634109,
        4052799313904261962}, {
  64, times1, 257359198881356, 120875054227405, 2011895759027682422,
        2012014931360215503}, {
  32, times2, 291705506022294, 162134297192792, 2319535707505209857,
        2321009753483354451}, {
  32, times3, 291922315691409, 162234934150296, 1370930728180888261,
        4392719527011673456}, {
  6, times4, 60, 100, 2, 1}
};

GST_START_TEST (test_regression)
{
  GstClockTime m_num, m_den, internal, external;
  gdouble r_squared, rate, expect_rate;
  gint i;

  for (i = 0; i < G_N_ELEMENTS (times); i++) {
    fail_unless (gst_calculate_linear_regression (times[i].v, NULL, times[i].n,
            &m_num, &m_den, &external, &internal, &r_squared));

    GST_LOG ("xbase %" G_GUINT64_FORMAT " ybase %" G_GUINT64_FORMAT " rate = %"
        G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT " = %.10f r_squared %f\n",
        internal, external, m_num, m_den, (gdouble) (m_num) / (m_den),
        r_squared);

    /* Require high correlation */
    fail_unless (r_squared >= 0.9);

    fail_unless (internal == times[i].expect_internal,
        "Regression params %d fail. internal %" G_GUINT64_FORMAT
        " != expected %" G_GUINT64_FORMAT, i, internal,
        times[i].expect_internal);
    /* Rate must be within 1% tolerance */
    expect_rate = ((gdouble) (times[i].expect_num) / times[i].expect_denom);
    rate = ((gdouble) (m_num) / m_den);
    fail_unless ((expect_rate - rate) >= -0.1 && (expect_rate - rate) <= 0.1,
        "Regression params %d fail. Rate out of range. Expected %f, got %f",
        i, expect_rate, rate);
    fail_unless (external >= times[i].expect_external * 0.99 &&
        external <= times[i].expect_external * 1.01,
        "Regression params %d fail. external %" G_GUINT64_FORMAT
        " != expected %" G_GUINT64_FORMAT, i, external,
        times[i].expect_external);
  }
}

GST_END_TEST;

static Suite *
gst_utils_suite (void)
{
  Suite *s = suite_create ("GstUtils");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_buffer_probe_n_times);
  tcase_add_test (tc_chain, test_buffer_probe_once);
  tcase_add_test (tc_chain, test_math_scale);
  tcase_add_test (tc_chain, test_math_scale_round);
  tcase_add_test (tc_chain, test_math_scale_ceil);
  tcase_add_test (tc_chain, test_math_scale_uint64);
  tcase_add_test (tc_chain, test_math_scale_random);
#ifdef HAVE_GSL
#ifdef HAVE_GMP
  tcase_add_test (tc_chain, test_math_scale_gmp);
  tcase_add_test (tc_chain, test_math_scale_gmp_int);
#endif
#endif

  tcase_add_test (tc_chain, test_guint64_to_gdouble);
  tcase_add_test (tc_chain, test_gdouble_to_guint64);
#ifndef GST_DISABLE_PARSE
  tcase_add_test (tc_chain, test_parse_bin_from_description);
#endif
  tcase_add_test (tc_chain, test_element_found_tags);
  tcase_add_test (tc_chain, test_element_link);
  tcase_add_test (tc_chain, test_element_link_with_ghost_pads);
  tcase_add_test (tc_chain, test_element_unlink);
  tcase_add_test (tc_chain, test_element_get_compatible_pad_request);
  tcase_add_test (tc_chain, test_set_value_from_string);
  tcase_add_test (tc_chain, test_binary_search);

  tcase_add_test (tc_chain, test_pad_proxy_query_caps_aggregation);
  tcase_add_test (tc_chain, test_greatest_common_divisor);

  tcase_add_test (tc_chain, test_read_macros);
  tcase_add_test (tc_chain, test_write_macros);
  tcase_add_test (tc_chain, test_regression);

  return s;
}

GST_CHECK_MAIN (gst_utils);
