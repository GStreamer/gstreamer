/* GStreamer unit tests for the interleave element
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
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
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_create_and_unref)
{
  GstElement *deinterleave;

  deinterleave = gst_element_factory_make ("deinterleave", NULL);
  fail_unless (deinterleave != NULL);

  gst_element_set_state (deinterleave, GST_STATE_NULL);
  gst_object_unref (deinterleave);
}

GST_END_TEST;

static GstPad *mysrcpad, **mysinkpads;
static gint nsinkpads;
static GstBus *bus;
static GstElement *deinterleave;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "width = (int) 32, "
        "channels = (int) 1, "
        "rate = (int) {32000, 48000}, " "endianness = (int) BYTE_ORDER"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "width = (int) 32, "
        "channels = (int) 2, "
        "rate = (int) {32000, 48000}, " "endianness = (int) BYTE_ORDER"));

#define CAPS_32khz \
         "audio/x-raw-float, " \
        "width = (int) 32, " \
        "channels = (int) 2, " \
        "rate = (int) 32000, " \
	"endianness = (int) BYTE_ORDER"

#define CAPS_48khz \
         "audio/x-raw-float, " \
        "width = (int) 32, " \
        "channels = (int) 2, " \
        "rate = (int) 48000, " \
	"endianness = (int) BYTE_ORDER"

#define CAPS_48khz_3CH \
         "audio/x-raw-float, " \
        "width = (int) 32, " \
        "channels = (int) 3, " \
        "rate = (int) 48000, " \
	"endianness = (int) BYTE_ORDER"

static GstFlowReturn
deinterleave_chain_func (GstPad * pad, GstBuffer * buffer)
{
  gint i;
  gfloat *indata;

  fail_unless (GST_IS_BUFFER (buffer));
  fail_unless_equals_int (GST_BUFFER_SIZE (buffer), 48000 * sizeof (gfloat));
  fail_unless (GST_BUFFER_DATA (buffer) != NULL);

  indata = (gfloat *) GST_BUFFER_DATA (buffer);

  if (strcmp (GST_PAD_NAME (pad), "sink0") == 0) {
    for (i = 0; i < 48000; i++)
      fail_unless_equals_float (indata[i], -1.0);
  } else if (strcmp (GST_PAD_NAME (pad), "sink1") == 0) {
    for (i = 0; i < 48000; i++)
      fail_unless_equals_float (indata[i], 1.0);
  } else {
    g_assert_not_reached ();
  }

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static void
deinterleave_pad_added (GstElement * src, GstPad * pad, gpointer data)
{
  gchar *name;
  gint link = GPOINTER_TO_INT (data);

  if (nsinkpads >= link)
    return;

  name = g_strdup_printf ("sink%d", nsinkpads);

  mysinkpads[nsinkpads] =
      gst_pad_new_from_static_template (&sinktemplate, name);
  g_free (name);
  fail_if (mysinkpads[nsinkpads] == NULL);

  gst_pad_set_chain_function (mysinkpads[nsinkpads], deinterleave_chain_func);
  fail_unless (gst_pad_link (pad, mysinkpads[nsinkpads]) == GST_PAD_LINK_OK);
  gst_pad_set_active (mysinkpads[nsinkpads], TRUE);
  nsinkpads++;
}

GST_START_TEST (test_2_channels)
{
  GstPad *sinkpad;
  gint i;
  GstBuffer *inbuf;
  GstCaps *caps;
  gfloat *indata;

  mysinkpads = g_new0 (GstPad *, 2);
  nsinkpads = 0;

  deinterleave = gst_element_factory_make ("deinterleave", NULL);
  fail_unless (deinterleave != NULL);

  mysrcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  fail_unless (mysrcpad != NULL);

  caps = gst_caps_from_string (CAPS_48khz);
  fail_unless (gst_pad_set_caps (mysrcpad, caps));
  gst_pad_use_fixed_caps (mysrcpad);

  sinkpad = gst_element_get_pad (deinterleave, "sink");
  fail_unless (sinkpad != NULL);
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK);
  g_object_unref (sinkpad);

  g_signal_connect (deinterleave, "pad-added",
      G_CALLBACK (deinterleave_pad_added), GINT_TO_POINTER (2));

  bus = gst_bus_new ();
  gst_element_set_bus (deinterleave, bus);

  fail_unless (gst_element_set_state (deinterleave,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  inbuf = gst_buffer_new_and_alloc (2 * 48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 2 * 48000; i += 2) {
    indata[i] = -1.0;
    indata[i + 1] = 1.0;
  }
  gst_buffer_set_caps (inbuf, caps);

  fail_unless (gst_pad_push (mysrcpad, inbuf) == GST_FLOW_OK);

  fail_unless (gst_element_set_state (deinterleave,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  for (i = 0; i < nsinkpads; i++)
    g_object_unref (mysinkpads[i]);
  g_free (mysinkpads);
  mysinkpads = NULL;

  g_object_unref (deinterleave);
  g_object_unref (bus);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_2_channels_1_linked)
{
  GstPad *sinkpad;
  gint i;
  GstBuffer *inbuf;
  GstCaps *caps;
  gfloat *indata;

  nsinkpads = 0;
  mysinkpads = g_new0 (GstPad *, 2);

  deinterleave = gst_element_factory_make ("deinterleave", NULL);
  fail_unless (deinterleave != NULL);

  mysrcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  fail_unless (mysrcpad != NULL);

  caps = gst_caps_from_string (CAPS_48khz);
  fail_unless (gst_pad_set_caps (mysrcpad, caps));
  gst_pad_use_fixed_caps (mysrcpad);

  sinkpad = gst_element_get_pad (deinterleave, "sink");
  fail_unless (sinkpad != NULL);
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK);
  g_object_unref (sinkpad);

  g_signal_connect (deinterleave, "pad-added",
      G_CALLBACK (deinterleave_pad_added), GINT_TO_POINTER (1));

  bus = gst_bus_new ();
  gst_element_set_bus (deinterleave, bus);

  fail_unless (gst_element_set_state (deinterleave,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  inbuf = gst_buffer_new_and_alloc (2 * 48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 2 * 48000; i += 2) {
    indata[i] = -1.0;
    indata[i + 1] = 1.0;
  }
  gst_buffer_set_caps (inbuf, caps);

  fail_unless (gst_pad_push (mysrcpad, inbuf) == GST_FLOW_OK);

  fail_unless (gst_element_set_state (deinterleave,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  for (i = 0; i < nsinkpads; i++)
    g_object_unref (mysinkpads[i]);
  g_free (mysinkpads);
  mysinkpads = NULL;

  g_object_unref (deinterleave);
  g_object_unref (bus);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_2_channels_caps_change)
{
  GstPad *sinkpad;
  GstCaps *caps, *caps2;
  gint i;
  GstBuffer *inbuf;
  gfloat *indata;

  nsinkpads = 0;
  mysinkpads = g_new0 (GstPad *, 2);

  deinterleave = gst_element_factory_make ("deinterleave", NULL);
  fail_unless (deinterleave != NULL);

  mysrcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  fail_unless (mysrcpad != NULL);

  caps = gst_caps_from_string (CAPS_48khz);
  fail_unless (gst_pad_set_caps (mysrcpad, caps));
  gst_pad_use_fixed_caps (mysrcpad);

  sinkpad = gst_element_get_pad (deinterleave, "sink");
  fail_unless (sinkpad != NULL);
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK);
  g_object_unref (sinkpad);

  g_signal_connect (deinterleave, "pad-added",
      G_CALLBACK (deinterleave_pad_added), GINT_TO_POINTER (2));

  bus = gst_bus_new ();
  gst_element_set_bus (deinterleave, bus);

  fail_unless (gst_element_set_state (deinterleave,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  inbuf = gst_buffer_new_and_alloc (2 * 48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 2 * 48000; i += 2) {
    indata[i] = -1.0;
    indata[i + 1] = 1.0;
  }
  gst_buffer_set_caps (inbuf, caps);

  fail_unless (gst_pad_push (mysrcpad, inbuf) == GST_FLOW_OK);

  caps2 = gst_caps_from_string (CAPS_32khz);
  gst_pad_set_caps (mysrcpad, caps2);

  inbuf = gst_buffer_new_and_alloc (2 * 48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 2 * 48000; i += 2) {
    indata[i] = -1.0;
    indata[i + 1] = 1.0;
  }
  gst_buffer_set_caps (inbuf, caps2);

  /* Should work fine because the caps changed in a compatible way */
  fail_unless (gst_pad_push (mysrcpad, inbuf) == GST_FLOW_OK);

  gst_caps_unref (caps2);

  caps2 = gst_caps_from_string (CAPS_48khz_3CH);
  gst_pad_set_caps (mysrcpad, caps2);

  inbuf = gst_buffer_new_and_alloc (3 * 48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 3 * 48000; i += 3) {
    indata[i] = -1.0;
    indata[i + 1] = 1.0;
    indata[i + 2] = 0.0;
  }
  gst_buffer_set_caps (inbuf, caps2);

  /* Should break because the caps changed in an incompatible way */
  fail_if (gst_pad_push (mysrcpad, inbuf) == GST_FLOW_OK);

  fail_unless (gst_element_set_state (deinterleave,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  for (i = 0; i < nsinkpads; i++)
    g_object_unref (mysinkpads[i]);
  g_free (mysinkpads);
  mysinkpads = NULL;

  g_object_unref (deinterleave);
  g_object_unref (bus);
  gst_caps_unref (caps);
  gst_caps_unref (caps2);
}

GST_END_TEST;


static Suite *
deinterleave_suite (void)
{
  Suite *s = suite_create ("deinterleave");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_create_and_unref);
  tcase_add_test (tc_chain, test_2_channels);
  tcase_add_test (tc_chain, test_2_channels_1_linked);
  tcase_add_test (tc_chain, test_2_channels_caps_change);

  return s;
}

GST_CHECK_MAIN (deinterleave);
