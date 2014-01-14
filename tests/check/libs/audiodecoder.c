/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <ts.santos@sisa.samsung.com>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/audio/audio.h>
#include <gst/app/app.h>

static GstPad *mysrcpad, *mysinkpad;
static GstElement *dec;
static GList *events = NULL;

#define TEST_MSECS_PER_SAMPLE 44100

#define GST_AUDIO_DECODER_TESTER_TYPE gst_audio_decoder_tester_get_type()
static GType gst_audio_decoder_tester_get_type (void);

typedef struct _GstAudioDecoderTester GstAudioDecoderTester;
typedef struct _GstAudioDecoderTesterClass GstAudioDecoderTesterClass;

struct _GstAudioDecoderTester
{
  GstAudioDecoder parent;

  gboolean setoutputformat_on_decoding;
};

struct _GstAudioDecoderTesterClass
{
  GstAudioDecoderClass parent_class;
};

G_DEFINE_TYPE (GstAudioDecoderTester, gst_audio_decoder_tester,
    GST_TYPE_AUDIO_DECODER);

static gboolean
gst_audio_decoder_tester_start (GstAudioDecoder * dec)
{
  return TRUE;
}

static gboolean
gst_audio_decoder_tester_stop (GstAudioDecoder * dec)
{
  return TRUE;
}

static void
gst_audio_decoder_tester_flush (GstAudioDecoder * dec, gboolean hard)
{
}

static gboolean
gst_audio_decoder_tester_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstAudioDecoderTester *tester = (GstAudioDecoderTester *) dec;
  GstAudioInfo info;
  gst_caps_unref (caps);

  if (!tester->setoutputformat_on_decoding) {
    caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
        "channels", G_TYPE_INT, 2, "rate", G_TYPE_INT, 44100,
        "layout", G_TYPE_STRING, "interleaved", NULL);
    gst_audio_info_from_caps (&info, caps);
    gst_caps_unref (caps);

    gst_audio_decoder_set_output_format (dec, &info);
  }
  return TRUE;
}

static GstFlowReturn
gst_audio_decoder_tester_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer)
{
  GstAudioDecoderTester *tester = (GstAudioDecoderTester *) dec;
  guint8 *data;
  gint size;
  GstMapInfo map;
  GstBuffer *output_buffer;
  guint64 samples;

  if (buffer == NULL)
    return GST_FLOW_OK;

  if (tester->setoutputformat_on_decoding) {
    GstCaps *caps;
    GstAudioInfo info;

    caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
        "channels", G_TYPE_INT, 2, "rate", G_TYPE_INT, 44100,
        "layout", G_TYPE_STRING, "interleaved", NULL);
    gst_audio_info_from_caps (&info, caps);
    gst_caps_unref (caps);

    gst_audio_decoder_set_output_format (dec, &info);
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  /* the output is SE16LE stereo 44100 Hz */
  samples =
      gst_util_uint64_scale_round (44100, GST_BUFFER_DURATION (buffer),
      GST_SECOND);
  size = 2 * 2 * samples;
  data = g_malloc0 (size);

  memcpy (data, map.data, sizeof (guint64));

  output_buffer = gst_buffer_new_wrapped (data, size);

  gst_buffer_unmap (buffer, &map);

  return gst_audio_decoder_finish_frame (dec, output_buffer, 1);
}

static void
gst_audio_decoder_tester_class_init (GstAudioDecoderTesterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audiosink_class = GST_AUDIO_DECODER_CLASS (klass);

  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-test-custom"));

  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw"));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));

  gst_element_class_set_metadata (element_class,
      "AudioDecoderTester", "Decoder/Audio", "yep", "me");

  audiosink_class->start = gst_audio_decoder_tester_start;
  audiosink_class->stop = gst_audio_decoder_tester_stop;
  audiosink_class->flush = gst_audio_decoder_tester_flush;
  audiosink_class->handle_frame = gst_audio_decoder_tester_handle_frame;
  audiosink_class->set_format = gst_audio_decoder_tester_set_format;
}

static void
gst_audio_decoder_tester_init (GstAudioDecoderTester * tester)
{
}

static gboolean
_mysinkpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  events = g_list_append (events, event);
  return TRUE;
}

static void
setup_audiodecodertester (void)
{
  GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw, format=(string)S16LE, "
          "rate=(int)[1, 320000], channels=(int)[1, 32],"
          "layout=(string)interleaved")
      );
  GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-test-custom")
      );

  dec = g_object_new (GST_AUDIO_DECODER_TESTER_TYPE, NULL);
  mysrcpad = gst_check_setup_src_pad (dec, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (dec, &sinktemplate);

  gst_pad_set_event_function (mysinkpad, _mysinkpad_event);
}

static void
cleanup_audiodecodertest (void)
{
  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (dec);
  gst_check_teardown_sink_pad (dec);
  gst_check_teardown_element (dec);
}

static GstBuffer *
create_test_buffer (guint64 num)
{
  GstBuffer *buffer;
  guint64 *data = g_malloc (sizeof (guint64));

  *data = num;

  buffer = gst_buffer_new_wrapped (data, sizeof (guint64));

  GST_BUFFER_PTS (buffer) =
      gst_util_uint64_scale_round (num, GST_SECOND, TEST_MSECS_PER_SAMPLE);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_round (1, GST_SECOND, TEST_MSECS_PER_SAMPLE);

  return buffer;
}

static void
send_startup_events (void)
{
  GstCaps *caps;

  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_stream_start ("randomvalue")));

  /* push caps */
  caps =
      gst_caps_new_simple ("audio/x-test-custom", "channels", G_TYPE_INT, 2,
      "rate", G_TYPE_INT, 44100, NULL);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));

}

#define NUM_BUFFERS 1000
GST_START_TEST (audiodecoder_playback)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;

  setup_audiodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  for (i = 0; i < NUM_BUFFERS; i++) {
    GstMapInfo map;
    guint64 num;

    buffer = create_test_buffer (i);

    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

    /* check that buffer was received by our source pad */
    buffer = buffers->data;

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    num = *(guint64 *) map.data;
    fail_unless (i == num);
    fail_unless (GST_BUFFER_PTS (buffer) == gst_util_uint64_scale_round (i,
            GST_SECOND, TEST_MSECS_PER_SAMPLE));
    fail_unless (GST_BUFFER_DURATION (buffer) == gst_util_uint64_scale_round (1,
            GST_SECOND, TEST_MSECS_PER_SAMPLE));

    gst_buffer_unmap (buffer, &map);

    gst_buffer_unref (buffer);
    buffers = g_list_delete_link (buffers, buffers);
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless (buffers == NULL);

  cleanup_audiodecodertest ();
}

GST_END_TEST;

static void
check_audiodecoder_negotiation (void)
{
  gboolean received_caps = FALSE;
  GList *iter;

  for (iter = events; iter; iter = g_list_next (iter)) {
    GstEvent *event = iter->data;

    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
      GstCaps *caps;
      GstStructure *structure;
      gint channels;
      gint rate;

      gst_event_parse_caps (event, &caps);
      structure = gst_caps_get_structure (caps, 0);

      fail_unless (gst_structure_get_int (structure, "rate", &rate));
      fail_unless (gst_structure_get_int (structure, "channels", &channels));

      fail_unless (rate == 44100, "%d != %d", rate, 44100);
      fail_unless (channels == 2, "%d != %d", channels, 2);

      received_caps = TRUE;
      break;
    }
  }
  fail_unless (received_caps);
}

GST_START_TEST (audiodecoder_negotiation_with_buffer)
{
  GstSegment segment;
  GstBuffer *buffer;

  setup_audiodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push a buffer event to force audiodecoder to push a caps event */
  buffer = create_test_buffer (0);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  check_audiodecoder_negotiation ();

  cleanup_audiodecodertest ();
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
}

GST_END_TEST;


GST_START_TEST (audiodecoder_negotiation_with_gap_event)
{
  GstSegment segment;

  setup_audiodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push a gap event to force audiodecoder to push a caps event */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_gap (0,
              GST_SECOND)));
  fail_unless (buffers == NULL);

  check_audiodecoder_negotiation ();

  cleanup_audiodecodertest ();
}

GST_END_TEST;


GST_START_TEST (audiodecoder_delayed_negotiation_with_gap_event)
{
  GstSegment segment;

  setup_audiodecodertester ();

  ((GstAudioDecoderTester *) dec)->setoutputformat_on_decoding = TRUE;

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push a gap event to force audiodecoder to push a caps event */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_gap (0,
              GST_SECOND)));
  fail_unless (buffers == NULL);

  check_audiodecoder_negotiation ();

  cleanup_audiodecodertest ();
}

GST_END_TEST;


static Suite *
gst_audiodecoder_suite (void)
{
  Suite *s = suite_create ("GstAudioDecoder");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, audiodecoder_playback);
  tcase_add_test (tc, audiodecoder_negotiation_with_buffer);
  tcase_add_test (tc, audiodecoder_negotiation_with_gap_event);
  tcase_add_test (tc, audiodecoder_delayed_negotiation_with_gap_event);

  return s;
}

GST_CHECK_MAIN (gst_audiodecoder);
