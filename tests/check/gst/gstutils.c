/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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
data_probe (GstPad * pad, GstMiniObject * obj, gpointer data)
{
  n_data_probes++;
  g_assert (GST_IS_MINI_OBJECT (obj));
  g_assert (data == SPECIAL_POINTER (0));
  return TRUE;
}

static gboolean
buffer_probe (GstPad * pad, GstBuffer * obj, gpointer data)
{
  n_buffer_probes++;
  g_assert (GST_IS_BUFFER (obj));
  g_assert (data == SPECIAL_POINTER (1));
  return TRUE;
}

static gboolean
event_probe (GstPad * pad, GstEvent * obj, gpointer data)
{
  n_event_probes++;
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

  pad = gst_element_get_pad (fakesink, "sink");
  gst_pad_add_data_probe (pad, G_CALLBACK (data_probe), SPECIAL_POINTER (0));
  gst_pad_add_buffer_probe (pad, G_CALLBACK (buffer_probe),
      SPECIAL_POINTER (1));
  gst_pad_add_event_probe (pad, G_CALLBACK (event_probe), SPECIAL_POINTER (2));
  gst_object_unref (pad);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);
  message = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
  gst_message_unref (message);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_assert (n_buffer_probes == 10);     /* one for every buffer */
  g_assert (n_event_probes == 2);       /* new segment and eos */
  g_assert (n_data_probes == 12);       /* duh */
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

  pad = gst_element_get_pad (fakesink, "sink");
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

  i = 1000000;
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
  guint64 from[] = { 0, 1, 100, 10000, G_GINT64_CONSTANT (1) << 63,
    (G_GINT64_CONSTANT (1) << 63) + 1,
    (G_GINT64_CONSTANT (1) << 63) + (G_GINT64_CONSTANT (1) << 62)
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
  guint64 to[] = { 0, 1, 100, 10000, G_GINT64_CONSTANT (1) << 63,
    (G_GINT64_CONSTANT (1) << 63) + 1,
    (G_GINT64_CONSTANT (1) << 63) + (G_GINT64_CONSTANT (1) << 62)
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

Suite *
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
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_utils_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
