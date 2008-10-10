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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/check/gstcheck.h>

#define SPECIAL_POINTER(x) ((void*)(19283847+(x)))

static int n_data_probes = 0;
static int n_buffer_probes = 0;
static int n_event_probes = 0;

static gboolean
probe_do_nothing (GstPad * pad, GstMiniObject * obj, gpointer data)
{
  GST_DEBUG_OBJECT (pad, "is buffer:%d", GST_IS_BUFFER (obj));
  return TRUE;
}

static gboolean
data_probe (GstPad * pad, GstMiniObject * obj, gpointer data)
{
  n_data_probes++;
  GST_DEBUG_OBJECT (pad, "data probe %d", n_data_probes);
  g_assert (GST_IS_MINI_OBJECT (obj));
  g_assert (data == SPECIAL_POINTER (0));
  return TRUE;
}

static gboolean
buffer_probe (GstPad * pad, GstBuffer * obj, gpointer data)
{
  n_buffer_probes++;
  GST_DEBUG_OBJECT (pad, "buffer probe %d", n_buffer_probes);
  g_assert (GST_IS_BUFFER (obj));
  g_assert (data == SPECIAL_POINTER (1));
  return TRUE;
}

static gboolean
event_probe (GstPad * pad, GstEvent * obj, gpointer data)
{
  n_event_probes++;
  GST_DEBUG_OBJECT (pad, "event probe %d [%s]",
      n_event_probes, GST_EVENT_TYPE_NAME (obj));
  g_assert (GST_IS_EVENT (obj));
  g_assert (data == SPECIAL_POINTER (2));
  return TRUE;
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
  gst_pad_add_data_probe (pad, G_CALLBACK (data_probe), SPECIAL_POINTER (0));
  gst_pad_add_buffer_probe (pad, G_CALLBACK (buffer_probe),
      SPECIAL_POINTER (1));
  gst_pad_add_event_probe (pad, G_CALLBACK (event_probe), SPECIAL_POINTER (2));

  /* add some probes just to test that _full works and the data is free'd
   * properly as it should be */
  gst_pad_add_data_probe_full (pad, G_CALLBACK (probe_do_nothing),
      g_strdup ("data probe string"), (GDestroyNotify) g_free);
  gst_pad_add_buffer_probe_full (pad, G_CALLBACK (probe_do_nothing),
      g_strdup ("buffer probe string"), (GDestroyNotify) g_free);
  gst_pad_add_event_probe_full (pad, G_CALLBACK (probe_do_nothing),
      g_strdup ("event probe string"), (GDestroyNotify) g_free);

  gst_object_unref (pad);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);
  message = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
  gst_message_unref (message);
  gst_object_unref (bus);

  g_assert (n_buffer_probes == 10);     /* one for every buffer */
  g_assert (n_event_probes == 3);       /* new segment, latency and eos */
  g_assert (n_data_probes == 13);       /* duh */

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  /* make sure nothing was sent in addition to the above when shutting down */
  g_assert (n_buffer_probes == 10);     /* one for every buffer */
  g_assert (n_event_probes == 3);       /* new segment, latency and eos */
  g_assert (n_data_probes == 13);       /* duh */
} GST_END_TEST;

static int n_data_probes_once = 0;
static int n_buffer_probes_once = 0;
static int n_event_probes_once = 0;

static gboolean
data_probe_once (GstPad * pad, GstMiniObject * obj, guint * data)
{
  n_data_probes_once++;
  g_assert (GST_IS_MINI_OBJECT (obj));

  gst_pad_remove_data_probe (pad, *data);

  return TRUE;
}

static gboolean
buffer_probe_once (GstPad * pad, GstBuffer * obj, guint * data)
{
  n_buffer_probes_once++;
  g_assert (GST_IS_BUFFER (obj));

  gst_pad_remove_buffer_probe (pad, *data);

  return TRUE;
}

static gboolean
event_probe_once (GstPad * pad, GstEvent * obj, guint * data)
{
  n_event_probes_once++;
  g_assert (GST_IS_EVENT (obj));

  gst_pad_remove_event_probe (pad, *data);

  return TRUE;
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
  id1 = gst_pad_add_data_probe (pad, G_CALLBACK (data_probe_once), &id1);
  id2 = gst_pad_add_buffer_probe (pad, G_CALLBACK (buffer_probe_once), &id2);
  id3 = gst_pad_add_event_probe (pad, G_CALLBACK (event_probe_once), &id3);
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
      g_error ("FAILED: expted '%s', got '%s' for bin '%s'",
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

  pipeline = gst_element_factory_make ("pipeline", NULL);
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  list = gst_tag_list_new ();

  g_object_set (fakesrc, "num-buffers", (int) 10, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_element_found_tags (GST_ELEMENT (fakesrc), list);

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

static Suite *
gst_utils_suite (void)
{
  Suite *s = suite_create ("GstUtils");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_buffer_probe_n_times);
  tcase_add_test (tc_chain, test_buffer_probe_once);
  tcase_add_test (tc_chain, test_math_scale);
  tcase_add_test (tc_chain, test_math_scale_uint64);
  tcase_add_test (tc_chain, test_math_scale_random);
  tcase_add_test (tc_chain, test_guint64_to_gdouble);
  tcase_add_test (tc_chain, test_gdouble_to_guint64);
#ifndef GST_DISABLE_PARSE
  tcase_add_test (tc_chain, test_parse_bin_from_description);
#endif
  tcase_add_test (tc_chain, test_element_found_tags);
  tcase_add_test (tc_chain, test_element_unlink);
  tcase_add_test (tc_chain, test_set_value_from_string);
  return s;
}

GST_CHECK_MAIN (gst_utils);
