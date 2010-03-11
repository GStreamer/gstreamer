/* GStreamer unit tests for the interleave element
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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
#include <gst/audio/multichannel.h>

GST_START_TEST (test_create_and_unref)
{
  GstElement *interleave;

  interleave = gst_element_factory_make ("interleave", NULL);
  fail_unless (interleave != NULL);

  gst_element_set_state (interleave, GST_STATE_NULL);
  gst_object_unref (interleave);
}

GST_END_TEST;

GST_START_TEST (test_request_pads)
{
  GstElement *interleave;

  GstPad *pad1, *pad2;

  interleave = gst_element_factory_make ("interleave", NULL);
  fail_unless (interleave != NULL);

  pad1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (pad1 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (pad1), "sink0");

  pad2 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (pad2 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (pad2), "sink1");

  gst_element_release_request_pad (interleave, pad2);
  gst_object_unref (pad2);
  gst_element_release_request_pad (interleave, pad1);
  gst_object_unref (pad1);

  gst_element_set_state (interleave, GST_STATE_NULL);
  gst_object_unref (interleave);
}

GST_END_TEST;

static GstPad **mysrcpads, *mysinkpad;

static GstBus *bus;

static GstElement *interleave;

static gint have_data;

static gfloat input[2];

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "width = (int) 32, "
        "channels = (int) 2, "
        "rate = (int) 48000, " "endianness = (int) BYTE_ORDER"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "width = (int) 32, "
        "channels = (int) 1, "
        "rate = (int) 48000, " "endianness = (int) BYTE_ORDER"));

#define CAPS_48khz \
         "audio/x-raw-float, " \
        "width = (int) 32, " \
        "channels = (int) 1, " \
        "rate = (int) 48000, " \
	"endianness = (int) BYTE_ORDER"

static GstFlowReturn
interleave_chain_func (GstPad * pad, GstBuffer * buffer)
{
  gfloat *outdata;

  gint i;

  fail_unless (GST_IS_BUFFER (buffer));
  fail_unless_equals_int (GST_BUFFER_SIZE (buffer),
      48000 * 2 * sizeof (gfloat));
  fail_unless (GST_BUFFER_DATA (buffer) != NULL);

  outdata = (gfloat *) GST_BUFFER_DATA (buffer);

  for (i = 0; i < 48000 * 2; i += 2) {
    fail_unless_equals_float (outdata[i], input[0]);
    fail_unless_equals_float (outdata[i + 1], input[1]);
  }

  have_data++;

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

GST_START_TEST (test_interleave_2ch)
{
  GstElement *queue;

  GstPad *sink0, *sink1, *src, *tmp;

  GstCaps *caps;

  gint i;

  GstBuffer *inbuf;

  gfloat *indata;

  mysrcpads = g_new0 (GstPad *, 2);

  have_data = 0;

  interleave = gst_element_factory_make ("interleave", NULL);
  fail_unless (interleave != NULL);

  queue = gst_element_factory_make ("queue", "queue");
  fail_unless (queue != NULL);

  sink0 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sink0 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (sink0), "sink0");

  sink1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sink1 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (sink1), "sink1");

  mysrcpads[0] = gst_pad_new_from_static_template (&srctemplate, "src0");
  fail_unless (mysrcpads[0] != NULL);

  caps = gst_caps_from_string (CAPS_48khz);
  fail_unless (gst_pad_set_caps (mysrcpads[0], caps));
  gst_pad_use_fixed_caps (mysrcpads[0]);

  mysrcpads[1] = gst_pad_new_from_static_template (&srctemplate, "src1");
  fail_unless (mysrcpads[1] != NULL);

  fail_unless (gst_pad_set_caps (mysrcpads[1], caps));
  gst_pad_use_fixed_caps (mysrcpads[1]);

  tmp = gst_element_get_static_pad (queue, "sink");
  fail_unless (gst_pad_link (mysrcpads[0], tmp) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  tmp = gst_element_get_static_pad (queue, "src");
  fail_unless (gst_pad_link (tmp, sink0) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  fail_unless (gst_pad_link (mysrcpads[1], sink1) == GST_PAD_LINK_OK);

  mysinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  fail_unless (mysinkpad != NULL);
  gst_pad_set_chain_function (mysinkpad, interleave_chain_func);
  gst_pad_set_active (mysinkpad, TRUE);

  src = gst_element_get_static_pad (interleave, "src");
  fail_unless (src != NULL);
  fail_unless (gst_pad_link (src, mysinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (src);

  bus = gst_bus_new ();
  gst_element_set_bus (interleave, bus);

  fail_unless (gst_element_set_state (interleave,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  input[0] = -1.0;
  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = -1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[0], inbuf) == GST_FLOW_OK);

  input[1] = 1.0;
  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = 1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[1], inbuf) == GST_FLOW_OK);

  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = -1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[0], inbuf) == GST_FLOW_OK);

  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = 1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[1], inbuf) == GST_FLOW_OK);

  fail_unless (have_data == 2);

  gst_object_unref (mysrcpads[0]);
  gst_object_unref (mysrcpads[1]);
  gst_object_unref (mysinkpad);

  gst_element_release_request_pad (interleave, sink0);
  gst_object_unref (sink0);
  gst_element_release_request_pad (interleave, sink1);
  gst_object_unref (sink1);

  gst_element_set_state (interleave, GST_STATE_NULL);
  gst_element_set_state (queue, GST_STATE_NULL);
  gst_object_unref (interleave);
  gst_object_unref (queue);
  gst_object_unref (bus);
  gst_caps_unref (caps);

  g_free (mysrcpads);
}

GST_END_TEST;

GST_START_TEST (test_interleave_2ch_1eos)
{
  GstElement *queue;

  GstPad *sink0, *sink1, *src, *tmp;

  GstCaps *caps;

  gint i;

  GstBuffer *inbuf;

  gfloat *indata;

  mysrcpads = g_new0 (GstPad *, 2);

  have_data = 0;

  interleave = gst_element_factory_make ("interleave", NULL);
  fail_unless (interleave != NULL);

  queue = gst_element_factory_make ("queue", "queue");
  fail_unless (queue != NULL);

  sink0 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sink0 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (sink0), "sink0");

  sink1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sink1 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (sink1), "sink1");

  mysrcpads[0] = gst_pad_new_from_static_template (&srctemplate, "src0");
  fail_unless (mysrcpads[0] != NULL);

  caps = gst_caps_from_string (CAPS_48khz);
  fail_unless (gst_pad_set_caps (mysrcpads[0], caps));
  gst_pad_use_fixed_caps (mysrcpads[0]);

  mysrcpads[1] = gst_pad_new_from_static_template (&srctemplate, "src1");
  fail_unless (mysrcpads[1] != NULL);

  fail_unless (gst_pad_set_caps (mysrcpads[1], caps));
  gst_pad_use_fixed_caps (mysrcpads[1]);

  tmp = gst_element_get_static_pad (queue, "sink");
  fail_unless (gst_pad_link (mysrcpads[0], tmp) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  tmp = gst_element_get_static_pad (queue, "src");
  fail_unless (gst_pad_link (tmp, sink0) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  fail_unless (gst_pad_link (mysrcpads[1], sink1) == GST_PAD_LINK_OK);

  mysinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  fail_unless (mysinkpad != NULL);
  gst_pad_set_chain_function (mysinkpad, interleave_chain_func);
  gst_pad_set_active (mysinkpad, TRUE);

  src = gst_element_get_static_pad (interleave, "src");
  fail_unless (src != NULL);
  fail_unless (gst_pad_link (src, mysinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (src);

  bus = gst_bus_new ();
  gst_element_set_bus (interleave, bus);

  fail_unless (gst_element_set_state (interleave,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  input[0] = -1.0;
  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = -1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[0], inbuf) == GST_FLOW_OK);

  input[1] = 1.0;
  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = 1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[1], inbuf) == GST_FLOW_OK);

  input[0] = 0.0;
  gst_pad_push_event (mysrcpads[0], gst_event_new_eos ());

  input[1] = 1.0;
  inbuf = gst_buffer_new_and_alloc (48000 * sizeof (gfloat));
  indata = (gfloat *) GST_BUFFER_DATA (inbuf);
  for (i = 0; i < 48000; i++)
    indata[i] = 1.0;
  gst_buffer_set_caps (inbuf, caps);
  fail_unless (gst_pad_push (mysrcpads[1], inbuf) == GST_FLOW_OK);

  fail_unless (have_data == 2);

  gst_object_unref (mysrcpads[0]);
  gst_object_unref (mysrcpads[1]);
  gst_object_unref (mysinkpad);

  gst_element_release_request_pad (interleave, sink0);
  gst_object_unref (sink0);
  gst_element_release_request_pad (interleave, sink1);
  gst_object_unref (sink1);

  gst_element_set_state (interleave, GST_STATE_NULL);
  gst_element_set_state (queue, GST_STATE_NULL);
  gst_object_unref (interleave);
  gst_object_unref (queue);
  gst_object_unref (bus);
  gst_caps_unref (caps);

  g_free (mysrcpads);
}

GST_END_TEST;

static void
src_handoff_float32 (GstElement * element, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  gint n = GPOINTER_TO_INT (user_data);
  GstCaps *caps;
  gfloat *data;
  gint i;

  if (GST_PAD_CAPS (pad))
    caps = gst_caps_ref (GST_PAD_CAPS (pad));
  else {
    caps = gst_caps_new_simple ("audio/x-raw-float",
        "width", G_TYPE_INT, 32,
        "channels", G_TYPE_INT, 1,
        "rate", G_TYPE_INT, 48000, "endianness", G_TYPE_INT, G_BYTE_ORDER,
        NULL);

    if (n == 2) {
      GstAudioChannelPosition pos[1] =
          { GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT };
      gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
    } else if (n == 3) {
      GstAudioChannelPosition pos[1] =
          { GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT };
      gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
    }
  }

  data = g_new (gfloat, 48000);
  GST_BUFFER_MALLOCDATA (buffer) = (guint8 *) data;
  GST_BUFFER_DATA (buffer) = (guint8 *) data;
  GST_BUFFER_SIZE (buffer) = 48000 * sizeof (gfloat);

  GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_DURATION (buffer) = GST_SECOND;

  GST_BUFFER_CAPS (buffer) = caps;

  for (i = 0; i < 48000; i++)
    data[i] = (n % 2 == 0) ? -1.0 : 1.0;
}

static void
sink_handoff_float32 (GstElement * element, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  gint i;
  gfloat *data;
  GstCaps *caps;
  gint n = GPOINTER_TO_INT (user_data);

  fail_unless (GST_IS_BUFFER (buffer));
  fail_unless_equals_int (GST_BUFFER_SIZE (buffer),
      48000 * 2 * sizeof (gfloat));
  fail_unless_equals_int (GST_BUFFER_DURATION (buffer), GST_SECOND);

  caps = gst_caps_new_simple ("audio/x-raw-float",
      "width", G_TYPE_INT, 32,
      "channels", G_TYPE_INT, 2,
      "rate", G_TYPE_INT, 48000, "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);

  if (n == 0) {
    GstAudioChannelPosition pos[2] =
        { GST_AUDIO_CHANNEL_POSITION_NONE, GST_AUDIO_CHANNEL_POSITION_NONE };
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  } else if (n == 1) {
    GstAudioChannelPosition pos[2] = { GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
    };
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  } else if (n == 2) {
    GstAudioChannelPosition pos[2] = { GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER
    };
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }

  fail_unless (gst_caps_is_equal (caps, GST_BUFFER_CAPS (buffer)));
  gst_caps_unref (caps);

  data = (gfloat *) GST_BUFFER_DATA (buffer);

  for (i = 0; i < 48000 * 2; i += 2) {
    fail_unless_equals_float (data[i], -1.0);
    fail_unless_equals_float (data[i + 1], 1.0);
  }

  have_data++;
}

GST_START_TEST (test_interleave_2ch_pipeline)
{
  GstElement *pipeline, *queue, *src1, *src2, *interleave, *sink;

  GstPad *sinkpad0, *sinkpad1, *tmp, *tmp2;

  GstMessage *msg;

  have_data = 0;

  pipeline = (GstElement *) gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  src1 = gst_element_factory_make ("fakesrc", "src1");
  fail_unless (src1 != NULL);
  g_object_set (src1, "num-buffers", 4, NULL);
  g_object_set (src1, "signal-handoffs", TRUE, NULL);
  g_signal_connect (src1, "handoff", G_CALLBACK (src_handoff_float32),
      GINT_TO_POINTER (0));
  gst_bin_add (GST_BIN (pipeline), src1);

  src2 = gst_element_factory_make ("fakesrc", "src2");
  fail_unless (src2 != NULL);
  g_object_set (src2, "num-buffers", 4, NULL);
  g_object_set (src2, "signal-handoffs", TRUE, NULL);
  g_signal_connect (src2, "handoff", G_CALLBACK (src_handoff_float32),
      GINT_TO_POINTER (1));
  gst_bin_add (GST_BIN (pipeline), src2);

  queue = gst_element_factory_make ("queue", "queue");
  fail_unless (queue != NULL);
  gst_bin_add (GST_BIN (pipeline), queue);

  interleave = gst_element_factory_make ("interleave", "interleave");
  fail_unless (interleave != NULL);
  gst_bin_add (GST_BIN (pipeline), gst_object_ref (interleave));

  sinkpad0 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sinkpad0 != NULL);
  tmp = gst_element_get_static_pad (src1, "src");
  fail_unless (gst_pad_link (tmp, sinkpad0) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  sinkpad1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sinkpad1 != NULL);
  tmp = gst_element_get_static_pad (src2, "src");
  tmp2 = gst_element_get_static_pad (queue, "sink");
  fail_unless (gst_pad_link (tmp, tmp2) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  gst_object_unref (tmp2);
  tmp = gst_element_get_static_pad (queue, "src");
  fail_unless (gst_pad_link (tmp, sinkpad1) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (sink != NULL);
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (sink_handoff_float32),
      GINT_TO_POINTER (0));
  gst_bin_add (GST_BIN (pipeline), sink);
  tmp = gst_element_get_static_pad (interleave, "src");
  tmp2 = gst_element_get_static_pad (sink, "sink");
  fail_unless (gst_pad_link (tmp, tmp2) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  gst_object_unref (tmp2);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline), GST_MESSAGE_EOS, -1);
  gst_message_unref (msg);

  fail_unless (have_data == 4);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_release_request_pad (interleave, sinkpad0);
  gst_object_unref (sinkpad0);
  gst_element_release_request_pad (interleave, sinkpad1);
  gst_object_unref (sinkpad1);
  gst_object_unref (interleave);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_interleave_2ch_pipeline_input_chanpos)
{
  GstElement *pipeline, *queue, *src1, *src2, *interleave, *sink;

  GstPad *sinkpad0, *sinkpad1, *tmp, *tmp2;

  GstMessage *msg;

  have_data = 0;

  pipeline = (GstElement *) gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  src1 = gst_element_factory_make ("fakesrc", "src1");
  fail_unless (src1 != NULL);
  g_object_set (src1, "num-buffers", 4, NULL);
  g_object_set (src1, "signal-handoffs", TRUE, NULL);
  g_signal_connect (src1, "handoff", G_CALLBACK (src_handoff_float32),
      GINT_TO_POINTER (2));
  gst_bin_add (GST_BIN (pipeline), src1);

  src2 = gst_element_factory_make ("fakesrc", "src2");
  fail_unless (src2 != NULL);
  g_object_set (src2, "num-buffers", 4, NULL);
  g_object_set (src2, "signal-handoffs", TRUE, NULL);
  g_signal_connect (src2, "handoff", G_CALLBACK (src_handoff_float32),
      GINT_TO_POINTER (3));
  gst_bin_add (GST_BIN (pipeline), src2);

  queue = gst_element_factory_make ("queue", "queue");
  fail_unless (queue != NULL);
  gst_bin_add (GST_BIN (pipeline), queue);

  interleave = gst_element_factory_make ("interleave", "interleave");
  fail_unless (interleave != NULL);
  g_object_set (interleave, "channel-positions-from-input", TRUE, NULL);
  gst_bin_add (GST_BIN (pipeline), gst_object_ref (interleave));

  sinkpad0 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sinkpad0 != NULL);
  tmp = gst_element_get_static_pad (src1, "src");
  fail_unless (gst_pad_link (tmp, sinkpad0) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  sinkpad1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sinkpad1 != NULL);
  tmp = gst_element_get_static_pad (src2, "src");
  tmp2 = gst_element_get_static_pad (queue, "sink");
  fail_unless (gst_pad_link (tmp, tmp2) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  gst_object_unref (tmp2);
  tmp = gst_element_get_static_pad (queue, "src");
  fail_unless (gst_pad_link (tmp, sinkpad1) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (sink != NULL);
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (sink_handoff_float32),
      GINT_TO_POINTER (1));
  gst_bin_add (GST_BIN (pipeline), sink);
  tmp = gst_element_get_static_pad (interleave, "src");
  tmp2 = gst_element_get_static_pad (sink, "sink");
  fail_unless (gst_pad_link (tmp, tmp2) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  gst_object_unref (tmp2);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline), GST_MESSAGE_EOS, -1);
  gst_message_unref (msg);

  fail_unless (have_data == 4);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_release_request_pad (interleave, sinkpad0);
  gst_object_unref (sinkpad0);
  gst_element_release_request_pad (interleave, sinkpad1);
  gst_object_unref (sinkpad1);
  gst_object_unref (interleave);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_interleave_2ch_pipeline_custom_chanpos)
{
  GstElement *pipeline, *queue, *src1, *src2, *interleave, *sink;

  GstPad *sinkpad0, *sinkpad1, *tmp, *tmp2;

  GstMessage *msg;

  GValueArray *arr;
  GValue val = { 0, };

  have_data = 0;

  pipeline = (GstElement *) gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  src1 = gst_element_factory_make ("fakesrc", "src1");
  fail_unless (src1 != NULL);
  g_object_set (src1, "num-buffers", 4, NULL);
  g_object_set (src1, "signal-handoffs", TRUE, NULL);
  g_signal_connect (src1, "handoff", G_CALLBACK (src_handoff_float32),
      GINT_TO_POINTER (0));
  gst_bin_add (GST_BIN (pipeline), src1);

  src2 = gst_element_factory_make ("fakesrc", "src2");
  fail_unless (src2 != NULL);
  g_object_set (src2, "num-buffers", 4, NULL);
  g_object_set (src2, "signal-handoffs", TRUE, NULL);
  g_signal_connect (src2, "handoff", G_CALLBACK (src_handoff_float32),
      GINT_TO_POINTER (1));
  gst_bin_add (GST_BIN (pipeline), src2);

  queue = gst_element_factory_make ("queue", "queue");
  fail_unless (queue != NULL);
  gst_bin_add (GST_BIN (pipeline), queue);

  interleave = gst_element_factory_make ("interleave", "interleave");
  fail_unless (interleave != NULL);
  g_object_set (interleave, "channel-positions-from-input", FALSE, NULL);
  arr = g_value_array_new (2);
  g_value_init (&val, GST_TYPE_AUDIO_CHANNEL_POSITION);
  g_value_set_enum (&val, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER);
  g_value_array_append (arr, &val);
  g_value_reset (&val);
  g_value_set_enum (&val, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER);
  g_value_array_append (arr, &val);
  g_value_unset (&val);
  g_object_set (interleave, "channel-positions", arr, NULL);
  g_value_array_free (arr);
  gst_bin_add (GST_BIN (pipeline), gst_object_ref (interleave));

  sinkpad0 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sinkpad0 != NULL);
  tmp = gst_element_get_static_pad (src1, "src");
  fail_unless (gst_pad_link (tmp, sinkpad0) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  sinkpad1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (sinkpad1 != NULL);
  tmp = gst_element_get_static_pad (src2, "src");
  tmp2 = gst_element_get_static_pad (queue, "sink");
  fail_unless (gst_pad_link (tmp, tmp2) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  gst_object_unref (tmp2);
  tmp = gst_element_get_static_pad (queue, "src");
  fail_unless (gst_pad_link (tmp, sinkpad1) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);

  sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (sink != NULL);
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (sink_handoff_float32),
      GINT_TO_POINTER (2));
  gst_bin_add (GST_BIN (pipeline), sink);
  tmp = gst_element_get_static_pad (interleave, "src");
  tmp2 = gst_element_get_static_pad (sink, "sink");
  fail_unless (gst_pad_link (tmp, tmp2) == GST_PAD_LINK_OK);
  gst_object_unref (tmp);
  gst_object_unref (tmp2);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline), GST_MESSAGE_EOS, -1);
  gst_message_unref (msg);

  fail_unless (have_data == 4);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_release_request_pad (interleave, sinkpad0);
  gst_object_unref (sinkpad0);
  gst_element_release_request_pad (interleave, sinkpad1);
  gst_object_unref (sinkpad1);
  gst_object_unref (interleave);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
interleave_suite (void)
{
  Suite *s = suite_create ("interleave");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 180);
  tcase_add_test (tc_chain, test_create_and_unref);
  tcase_add_test (tc_chain, test_request_pads);
  tcase_add_test (tc_chain, test_interleave_2ch);
  tcase_add_test (tc_chain, test_interleave_2ch_1eos);
  tcase_add_test (tc_chain, test_interleave_2ch_pipeline);
  tcase_add_test (tc_chain, test_interleave_2ch_pipeline_input_chanpos);
  tcase_add_test (tc_chain, test_interleave_2ch_pipeline_custom_chanpos);

  return s;
}

GST_CHECK_MAIN (interleave);
