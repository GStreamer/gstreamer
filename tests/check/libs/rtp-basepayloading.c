/* GStreamer RTP base payloader unit tests
 * Copyright (C) 2013 Sebastian Rasmussen <sebras@hotmail.com>
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
 * You should have received a copy of the GNU Library General
 * Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst-libs/gst/rtp/gstrtpbuffer.h>
#include <gst-libs/gst/rtp/gstrtpbasepayload.h>
#include <gst-libs/gst/rtp/gstrtpbasedepayload.h>

/* GstRtpDummyPay */

#define GST_TYPE_RTP_DUMMY_PAY \
  (gst_rtp_dummy_pay_get_type())
#define GST_RTP_DUMMY_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_DUMMY_PAY,GstRtpDummyPay))
#define GST_RTP_DUMMY_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_DUMMY_PAY,GstRtpDummyPayClass))
#define GST_IS_RTP_DUMMY_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_DUMMY_PAY))
#define GST_IS_RTP_DUMMY_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_DUMMY_PAY))

typedef struct _GstRtpDummyPay GstRtpDummyPay;
typedef struct _GstRtpDummyPayClass GstRtpDummyPayClass;

struct _GstRtpDummyPay
{
  GstRTPBasePayload payload;
};

struct _GstRtpDummyPayClass
{
  GstRTPBasePayloadClass parent_class;
};

GType gst_rtp_dummy_pay_get_type (void);

G_DEFINE_TYPE (GstRtpDummyPay, gst_rtp_dummy_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static GstFlowReturn gst_rtp_dummy_pay_handle_buffer (GstRTPBasePayload * pay,
    GstBuffer * buffer);

static GstStaticPadTemplate gst_rtp_dummy_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtp_dummy_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_rtp_dummy_pay_class_init (GstRtpDummyPayClass * klass)
{
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstrtpbasepayload_class = GST_RTP_BASE_PAYLOAD_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_dummy_pay_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_dummy_pay_src_template));

  gstrtpbasepayload_class->handle_buffer = gst_rtp_dummy_pay_handle_buffer;
}

static void
gst_rtp_dummy_pay_init (GstRtpDummyPay * pay)
{
  gst_rtp_base_payload_set_options (GST_RTP_BASE_PAYLOAD (pay), "application",
      TRUE, "dummy", 42);
}

static GstRtpDummyPay *
rtp_dummy_pay_new (void)
{
  return g_object_new (GST_TYPE_RTP_DUMMY_PAY, NULL);
}

static GstFlowReturn
gst_rtp_dummy_pay_handle_buffer (GstRTPBasePayload * pay, GstBuffer * buffer)
{
  GstBuffer *paybuffer;

  if (!gst_pad_has_current_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (pay))) {
    if (!gst_rtp_base_payload_set_outcaps (GST_RTP_BASE_PAYLOAD (pay), NULL)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  paybuffer = gst_rtp_buffer_new_allocate (0, 0, 0);
  GST_BUFFER_PTS (paybuffer) = GST_BUFFER_PTS (buffer);
  GST_BUFFER_OFFSET (paybuffer) = GST_BUFFER_OFFSET (buffer);
  gst_buffer_append (paybuffer, buffer);

  return gst_rtp_base_payload_push (pay, paybuffer);
}

/* GstRtpDummyDepay */

#define GST_TYPE_RTP_DUMMY_DEPAY \
  (gst_rtp_dummy_depay_get_type())
#define GST_RTP_DUMMY_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_DUMMY_DEPAY,GstRtpDummyDepay))
#define GST_RTP_DUMMY_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_DUMMY_DEPAY,GstRtpDummyDepayClass))
#define GST_IS_RTP_DUMMY_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_DUMMY_DEPAY))
#define GST_IS_RTP_DUMMY_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_DUMMY_DEPAY))

typedef struct _GstRtpDummyDepay GstRtpDummyDepay;
typedef struct _GstRtpDummyDepayClass GstRtpDummyDepayClass;

struct _GstRtpDummyDepay
{
  GstRTPBaseDepayload depayload;
};

struct _GstRtpDummyDepayClass
{
  GstRTPBaseDepayloadClass parent_class;
};

GType gst_rtp_dummy_depay_get_type (void);

G_DEFINE_TYPE (GstRtpDummyDepay, gst_rtp_dummy_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);

static GstBuffer *gst_rtp_dummy_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);

static GstStaticPadTemplate gst_rtp_dummy_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtp_dummy_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_rtp_dummy_depay_class_init (GstRtpDummyDepayClass * klass)
{
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstrtpbasedepayload_class = GST_RTP_BASE_DEPAYLOAD_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_dummy_depay_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_dummy_depay_src_template));

  gstrtpbasedepayload_class->process = gst_rtp_dummy_depay_process;
}

static void
gst_rtp_dummy_depay_init (GstRtpDummyDepay * depay)
{
}

static GstRtpDummyDepay *
rtp_dummy_depay_new (void)
{
  return g_object_new (GST_TYPE_RTP_DUMMY_DEPAY, NULL);
}

static GstBuffer *
gst_rtp_dummy_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRTPBuffer rtp = { NULL };
  GstBuffer *outbuf;

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  outbuf = gst_rtp_buffer_get_payload_buffer (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  return outbuf;
}

/* Tests */

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GMainLoop *mainloop = (GMainLoop *) user_data;
  gboolean result;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *debug;
      gchar *element_name;

      element_name = message->src ? gst_object_get_name (message->src) : NULL;
      gst_message_parse_error (message, &err, &debug);
      fail ("Error from element %s: %s\n%s", GST_STR_NULL (element_name),
          err->message, debug ? debug : "");
      g_error_free (err);
      g_free (debug);
      g_free (element_name);

      g_main_loop_quit (mainloop);
      result = FALSE;
    }
      break;

    case GST_MESSAGE_EOS:
      g_main_loop_quit (mainloop);
      result = FALSE;
      break;

    default:
      result = TRUE;
      break;
  }

  return result;
}

static void
push_buffer (GstElement * src, GstClockTime timestamp)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  GST_LOG ("pushing buffer %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));

  buf = gst_buffer_new_allocate (NULL, 0, NULL);
  GST_BUFFER_PTS (buf) = timestamp;
  g_signal_emit_by_name (src, "push-buffer", buf, &ret);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  gst_buffer_unref (buf);
}

static void
await_buffer (GstElement * sink, GstClockTime timestamp)
{
  GstSample *sample = NULL;
  GstBuffer *buf;

  GST_LOG ("awaiting buffer %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  fail_if (sample == NULL);
  buf = gst_sample_get_buffer (sample);
  GST_LOG ("got buffer %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buf)));
  fail_unless_equals_int (GST_BUFFER_PTS (buf), timestamp);
  gst_sample_unref (sample);
}

static void
push_eos (GstElement * src)
{
  GstFlowReturn ret;

  GST_LOG ("pushing EOS");

  g_signal_emit_by_name (src, "end-of-stream", &ret);
  fail_unless_equals_int (ret, GST_FLOW_OK);
}

static void
await_eos (GstElement * sink)
{
  GstSample *sample = NULL;
  gboolean eos;

  GST_LOG ("awaiting EOS");

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  fail_if (sample != NULL);
  g_object_get (sink, "eos", &eos, NULL);
  fail_unless (eos);
}

GST_START_TEST (rtp_base_test)
{
  GstElement *pipeline, *src, *pay, *depay, *sink;
  GMainLoop *mainloop;
  GstBus *bus;
  GstCaps *caps;
  GstStructure *stats;
  guint clock_rate;
  guint seqnum;
  guint timestamp;
  GObjectClass *klass;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (GST_IS_PIPELINE (pipeline));

  src = gst_element_factory_make ("appsrc", NULL);
  fail_unless (src != NULL);

  pay = GST_ELEMENT (rtp_dummy_pay_new ());
  fail_unless (GST_IS_RTP_DUMMY_PAY (pay));

  depay = GST_ELEMENT (rtp_dummy_depay_new ());
  fail_unless (GST_IS_RTP_DUMMY_DEPAY (depay));

  sink = gst_element_factory_make ("appsink", NULL);

  caps = gst_caps_from_string ("application/x-raw");
  g_object_set (src, "do-timestamp", TRUE, "caps", caps, "format",
      GST_FORMAT_TIME, NULL);
  gst_caps_unref (caps);

  g_object_set (pay, "seqnum-offset", 0, "timestamp-offset", 0, NULL);

  g_object_set (sink, "sync", FALSE, "emit-signals", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, pay, depay, sink, NULL);

  fail_unless (gst_element_link (src, pay));
  fail_unless (gst_element_link (pay, depay));
  fail_unless (gst_element_link (depay, sink));

  mainloop = g_main_loop_new (NULL, FALSE);
  fail_unless (mainloop != NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_callback, (gpointer) mainloop);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  fail_unless (gst_element_seek (src, 1.0, GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE,
          GST_CLOCK_TIME_NONE));

  push_buffer (src, 0 * GST_SECOND);
  await_buffer (sink, 0 * GST_SECOND);

  push_buffer (src, 1 * GST_SECOND);
  await_buffer (sink, 1 * GST_SECOND);

  klass = G_OBJECT_GET_CLASS (pay);
  fail_unless (g_object_class_find_property (klass, "stats") != NULL);

  g_object_get (pay, "stats", &stats, NULL);

  fail_unless (gst_structure_has_field (stats, "clock-rate"));
  fail_unless (gst_structure_has_field (stats, "seqnum"));
  fail_unless (gst_structure_has_field (stats, "timestamp"));

  fail_unless (gst_structure_get_uint (stats, "clock-rate", &clock_rate));
  fail_unless (gst_structure_get_uint (stats, "seqnum", &seqnum));
  fail_unless (gst_structure_get_uint (stats, "timestamp", &timestamp));

  fail_unless_equals_int (clock_rate, 42);
  fail_unless_equals_int (seqnum, 1);
  fail_unless_equals_int (timestamp, 42);

  push_eos (src);
  await_eos (sink);

  g_main_loop_run (mainloop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_main_loop_unref (mainloop);

  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
rtp_basepayloading_suite (void)
{
  Suite *s = suite_create ("rtp_base_payloading_test");
  TCase *tc_chain = tcase_create ("");

  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, rtp_base_test);

  return s;
}

GST_CHECK_MAIN (rtp_basepayloading)
