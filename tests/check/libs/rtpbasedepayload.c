/* GStreamer RTP base depayloader unit tests
 * Copyright (C) 2014 Sebastian Rasmussen <sebras@hotmail.com>
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
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtpbasedepayload.h>

#define DEFAULT_CLOCK_RATE (42)

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
  guint64 rtptime;
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
static gboolean gst_rtp_dummy_depay_set_caps (GstRTPBaseDepayload *filter,
    GstCaps *caps);

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
  gstrtpbasedepayload_class->set_caps = gst_rtp_dummy_depay_set_caps;
}

static void
gst_rtp_dummy_depay_init (GstRtpDummyDepay * depay)
{
  depay->rtptime = 0;
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
  guint32 rtptime;
  guint i;

  GST_LOG ("depayloading buffer pts=%" GST_TIME_FORMAT " offset=%"
  G_GUINT64_FORMAT " memories=%d", GST_TIME_ARGS (GST_BUFFER_PTS(buf)),
  GST_BUFFER_OFFSET(buf), gst_buffer_n_memory (buf));

  for (i = 0; i < gst_buffer_n_memory (buf); i++) {
    GstMemory *mem = gst_buffer_get_memory (buf, 0);
    gsize size, offset, maxsize;
    size = gst_memory_get_sizes (mem, &offset, &maxsize);
    GST_LOG ("\tsize=%zd offset=%zd maxsize=%zd", size, offset, maxsize);
    gst_memory_unref (mem);
  }

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  outbuf = gst_rtp_buffer_get_payload_buffer (&rtp);
  rtptime = gst_rtp_buffer_get_timestamp (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (buf);
  GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buf);

  GST_LOG ("depayloaded buffer pts=%" GST_TIME_FORMAT " offset=%"
      G_GUINT64_FORMAT " rtptime=%" G_GUINT32_FORMAT " memories=%d",
      GST_TIME_ARGS (GST_BUFFER_PTS(outbuf)),
      GST_BUFFER_OFFSET(outbuf), rtptime, gst_buffer_n_memory (buf));

  for (i = 0; i < gst_buffer_n_memory (buf); i++) {
    GstMemory *mem = gst_buffer_get_memory (buf, 0);
    gsize size, offset, maxsize;
    size = gst_memory_get_sizes (mem, &offset, &maxsize);
    GST_LOG ("\tsize=%zd offset=%zd maxsize=%zd", size, offset, maxsize);
    gst_memory_unref (mem);
  }

  return outbuf;
}

static gboolean
gst_rtp_dummy_depay_set_caps (GstRTPBaseDepayload *filter, GstCaps *caps)
{
  GstEvent *event;
  event = gst_event_new_caps (caps);
  gst_pad_push_event (filter->srcpad, event);
  return TRUE;
}

/* Helper functions and global state */

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

typedef struct State State;

struct State {
  GstElement *element;
  GstPad *sinkpad;
  GstPad *srcpad;
};

static GList *events;

static gboolean
event_func (GstPad * pad, GstObject * noparent, GstEvent * event)
{
  events = g_list_append (events, gst_event_ref (event));
  return gst_pad_event_default (pad, noparent, event);
}

static void drop_events (void)
{
  while (events != NULL) {
    gst_event_unref (GST_EVENT (events->data));
    events = g_list_delete_link (events, events);
  }
}

static void validate_events_received (guint received)
{
  fail_unless_equals_int (g_list_length (events), received);
}

static void validate_event (guint index, const gchar *name,
    const gchar *field, ...)
{
  GstEvent *event;
  va_list var_args;

  fail_if (index >= g_list_length (events));
  event = GST_EVENT (g_list_nth_data (events, index));
  fail_if (event == NULL);

  GST_TRACE ("%" GST_PTR_FORMAT, event);

  fail_unless_equals_string (GST_EVENT_TYPE_NAME (event), name);

  va_start (var_args, field);
  while (field) {
    if (!g_strcmp0 (field, "timestamp")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      GstClockTime timestamp, duration;
      gst_event_parse_gap (event, &timestamp, &duration);
      fail_unless_equals_uint64 (timestamp, expected);
    } else if (!g_strcmp0 (field, "duration")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      GstClockTime timestamp, duration;
      gst_event_parse_gap (event, &timestamp, &duration);
      fail_unless_equals_uint64 (duration, expected);
    } else if (!g_strcmp0 (field, "time")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      fail_unless_equals_uint64 (segment->time, expected);
    } else if (!g_strcmp0 (field, "start")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      fail_unless_equals_uint64 (segment->start, expected);
    } else if (!g_strcmp0 (field, "stop")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      fail_unless_equals_uint64 (segment->stop, expected);
    } else if (!g_strcmp0 (field, "applied-rate")) {
      gdouble expected = va_arg (var_args, gdouble);
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      fail_unless_equals_uint64 (segment->applied_rate, expected);
    } else if (!g_strcmp0 (field, "rate")) {
      gdouble expected = va_arg (var_args, gdouble);
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      fail_unless_equals_uint64 (segment->rate, expected);
    } else if (!g_strcmp0 (field, "media-type")) {
      const gchar *expected = va_arg (var_args, const gchar *);
      GstCaps *caps;
      const gchar *media_type;
      gst_event_parse_caps (event, &caps);
      media_type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
      fail_unless_equals_string (media_type, expected);
    } else if (!g_strcmp0 (field, "npt-start")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      GstCaps *caps;
      GstClockTime start;
      gst_event_parse_caps (event, &caps);
      fail_unless (gst_structure_get_clock_time (
            gst_caps_get_structure (caps, 0), "npt-start", &start));
      fail_unless_equals_uint64 (start, expected);
    } else if (!g_strcmp0 (field, "npt-stop")) {
      GstClockTime expected = va_arg (var_args, GstClockTime);
      GstCaps *caps;
      GstClockTime stop;
      gst_event_parse_caps (event, &caps);
      fail_unless (gst_structure_get_clock_time (
            gst_caps_get_structure (caps, 0), "npt-stop", &stop));
      fail_unless_equals_uint64 (stop, expected);
    } else if (!g_strcmp0 (field, "play-speed")) {
      gdouble expected = va_arg (var_args, gdouble);
      GstCaps *caps;
      gdouble speed;
      gst_event_parse_caps (event, &caps);
      fail_unless (gst_structure_get_double (
            gst_caps_get_structure (caps, 0), "play-speed", &speed));
      fail_unless (speed == expected);
    } else if (!g_strcmp0 (field, "play-scale")) {
      gdouble expected = va_arg (var_args, gdouble);
      GstCaps *caps;
      gdouble scale;
      gst_event_parse_caps (event, &caps);
      fail_unless (gst_structure_get_double (
            gst_caps_get_structure (caps, 0), "play-scale", &scale));
      fail_unless (scale == expected);
    } else {
      fail ("test cannot validate unknown event field '%s'", field);
    }
    field = va_arg (var_args, const gchar *);
  }
  va_end (var_args);
}

#define push_rtp_buffer(state, field, ...) \
	push_rtp_buffer_full ((state), GST_FLOW_OK, (field), __VA_ARGS__)
#define push_rtp_buffer_fails(state, error, field, ...) \
        push_rtp_buffer_full ((state), (error), (field), __VA_ARGS__)

static void push_rtp_buffer_full (State *state, GstFlowReturn expected,
    const gchar *field, ...)
{
  GstBuffer *buf = gst_rtp_buffer_new_allocate (0, 0, 0);
  GstRTPBuffer rtp = { NULL };
  gboolean mapped = FALSE;
  va_list var_args;

  va_start (var_args, field);
  while (field) {
    if (!g_strcmp0 (field, "pts")) {
      GstClockTime pts = va_arg (var_args, GstClockTime);
      GST_BUFFER_PTS (buf) = pts;
    } else if (!g_strcmp0 (field, "offset")) {
      guint64 offset = va_arg (var_args, guint64);
      GST_BUFFER_OFFSET (buf) = offset;
    } else if (!g_strcmp0 (field, "discont")) {
      gboolean discont = va_arg (var_args, gboolean);
      if (discont) {
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      } else {
        GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
      }
    } else {
      if (!mapped) {
        gst_rtp_buffer_map (buf, GST_MAP_WRITE, &rtp);
        mapped = TRUE;
      }
      if (!g_strcmp0 (field, "rtptime")) {
        guint32 rtptime = va_arg (var_args, guint64);
        gst_rtp_buffer_set_timestamp (&rtp, rtptime);
      } else if (!g_strcmp0 (field, "payload-type")) {
        guint payload_type = va_arg (var_args, guint);
        gst_rtp_buffer_set_payload_type (&rtp, payload_type);
      } else if (!g_strcmp0 (field, "seq")) {
        guint seq = va_arg (var_args, guint);
        gst_rtp_buffer_set_seq (&rtp, seq);
      } else if (!g_strcmp0 (field, "ssrc")) {
        guint32 ssrc = va_arg (var_args, guint);
        gst_rtp_buffer_set_ssrc (&rtp, ssrc);
      } else {
        fail ("test cannot set unknown buffer field '%s'", field);
      }
    }
    field = va_arg (var_args, const gchar *);
  }
  va_end (var_args);

  if (mapped) {
    gst_rtp_buffer_unmap (&rtp);
  }

  fail_unless_equals_int (gst_pad_push (state->srcpad, buf), expected);
}

#define push_buffer(state, field, ...) \
	push_buffer_full ((state), GST_FLOW_OK, (field), __VA_ARGS__)

static void push_buffer_full (State *state, GstFlowReturn expected,
    const gchar *field, ...)
{
  GstBuffer *buf = gst_buffer_new_allocate (0, 0, 0);
  va_list var_args;

  va_start (var_args, field);
  while (field) {
    if (!g_strcmp0 (field, "pts")) {
      GstClockTime pts = va_arg (var_args, GstClockTime);
      GST_BUFFER_PTS (buf) = pts;
    } else if (!g_strcmp0 (field, "offset")) {
      guint64 offset = va_arg (var_args, guint64);
      GST_BUFFER_OFFSET (buf) = offset;
    } else if (!g_strcmp0 (field, "discont")) {
      gboolean discont = va_arg (var_args, gboolean);
      if (discont) {
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      } else {
        GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
      }
    } else {
      fail ("test cannot set unknown buffer field '%s'", field);
    }
    field = va_arg (var_args, const gchar *);
  }
  va_end (var_args);

  fail_unless_equals_int (gst_pad_push (state->srcpad, buf), expected);
}

static void validate_buffers_received (guint received)
{
  fail_unless_equals_int (g_list_length (buffers), received);
}

static void validate_buffer (guint index, const gchar *field, ...)
{
  GstBuffer *buf;
  va_list var_args;

  fail_if (index >= g_list_length (buffers));
  buf = GST_BUFFER (g_list_nth_data (buffers, (index)));
  fail_if (buf == NULL);

  GST_TRACE ("%" GST_PTR_FORMAT, buf);

  va_start (var_args, field);
  while (field) {
    if (!g_strcmp0 (field, "pts")) {
      GstClockTime pts = va_arg (var_args, GstClockTime);
      fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), pts);
    } else if (!g_strcmp0 (field, "offset")) {
      guint64 offset = va_arg (var_args, guint64);
      fail_unless_equals_uint64 (GST_BUFFER_OFFSET(buf), offset);
    } else if (!g_strcmp0 (field, "discont")) {
      gboolean discont = va_arg (var_args, gboolean);
      if (discont) {
        fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
      } else {
        fail_if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
      }
    } else {
      fail ("test cannot validate unknown buffer field '%s'", field);
    }
    field = va_arg (var_args, const gchar *);
  }
  va_end (var_args);
}

static State *create_depayloader (const gchar *caps_str,
    const gchar *property, ...)
{
  va_list var_args;
  GstCaps *caps;
  State *state;

  state = g_new0 (State, 1);

  state->element = GST_ELEMENT (rtp_dummy_depay_new ());
  fail_unless (GST_IS_RTP_DUMMY_DEPAY (state->element));

  va_start (var_args, property);
  g_object_set_valist (G_OBJECT (state->element), property, var_args);
  va_end (var_args);

  state->srcpad = gst_check_setup_src_pad (state->element, &srctemplate);
  state->sinkpad = gst_check_setup_sink_pad (state->element, &sinktemplate);

  fail_unless (gst_pad_set_active (state->srcpad, TRUE));
  fail_unless (gst_pad_set_active (state->sinkpad, TRUE));

  if (caps_str) {
    caps = gst_caps_from_string (caps_str);
  } else {
    caps = NULL;
  }
  gst_check_setup_events (state->srcpad, state->element, caps, GST_FORMAT_TIME);
  if (caps) {
    gst_caps_unref (caps);
  }

  gst_pad_set_chain_function (state->sinkpad, gst_check_chain_func);
  gst_pad_set_event_function (state->sinkpad, event_func);

  return state;
}

static void set_state (State *state, GstState new_state)
{
  fail_unless_equals_int (gst_element_set_state (state->element, new_state),
      GST_STATE_CHANGE_SUCCESS);
}

static void packet_lost (State *state, GstClockTime timestamp,
    GstClockTime duration)
{
  GstEvent *event;
  guint seqnum = 0x4243;
  gboolean late = TRUE;
  guint retries = 42;

  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
    gst_structure_new ("GstRTPPacketLost",
      "seqnum", G_TYPE_UINT, seqnum,
      "timestamp", G_TYPE_UINT64, timestamp,
      "duration", G_TYPE_UINT64, duration,
      "late", G_TYPE_BOOLEAN, late,
      "retry", G_TYPE_UINT, retries,
      NULL));;

  fail_unless (gst_pad_push_event (state->srcpad, event));
}

static void reconfigure_caps (State *state, const gchar *caps_str)
{
  GstCaps *newcaps;
  GstEvent *event;
  newcaps = gst_caps_from_string (caps_str);
  event = gst_event_new_caps (newcaps);
  gst_caps_unref (newcaps);
  fail_unless (gst_pad_push_event (state->srcpad, event));
}

static void flush_pipeline (State *state)
{
  GstEvent *event;
  GstSegment segment;
  event = gst_event_new_flush_start ();
  fail_unless (gst_pad_push_event (state->srcpad, event));
  event = gst_event_new_flush_stop (TRUE);
  fail_unless (gst_pad_push_event (state->srcpad, event));
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  fail_unless (gst_pad_push_event (state->srcpad, event));
}

static void destroy_depayloader (State *state)
{
  gst_check_teardown_sink_pad (state->element);
  gst_check_teardown_src_pad (state->element);

  gst_check_drop_buffers ();
  drop_events ();

  g_object_unref (state->element);

  g_free (state);
}

/* Tests */

/* send two RTP packets having sequential sequence numbers and timestamps
 * differing by DEFAULT_CLOCK_RATE. the depayloader first pushes the normal
 * stream-start, caps and segment events downstream before processing each RTP
 * packet and pushing a corresponding buffer. PTS will be carried over from the
 * RTP packets by the payloader to the buffers. because the sequence numbers are
 * sequential then GST_BUFFER_FLAG_DISCONT will not be set for either buffer.
 */
GST_START_TEST (rtp_base_depayload_buffer_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 1,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_events_received (3);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* the intent with this test is to provide the depayloader with a buffer that
 * does not contain an RTP header. this makes it impossible for the depayloader
 * to depayload the incoming RTP packet, yet the stream-start and caps events
 * will still be pushed.
 */
GST_START_TEST (rtp_base_depayload_invalid_rtp_packet_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_buffer (state,
      "pts", 0 * GST_SECOND,
      "offset", GST_BUFFER_OFFSET_NONE,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (0);

  validate_events_received (2);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* validate what happens when a depayloader is provided with two RTP packets
 * sent after each other that do not have sequential sequence numbers. in this
 * case the depayloader should be able to depayload both first and the second
 * buffer, but the second buffer will have GST_BUFFER_FLAG_DISCONT set to
 * indicate that the was a discontinuity in the stream. the initial events are
 * pushed prior to the buffers arriving so they should be unaffected by the gap
 * in sequence numbers.
 */
GST_START_TEST (rtp_base_depayload_with_gap_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x43214321),
      "seq", 0x4242,
      NULL);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x43214321) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 2,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", TRUE,
      NULL);

  validate_events_received (3);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* two RTP packets are pushed in this test, and while the sequence numbers are
 * sequential they are reversed. the expectation is that the depayloader will be
 * able to depayload the first RTP packet, but once the second RTP packet
 * arrives it will be discarded because it arrived too late. the initial events
 * should be unaffected by the reversed buffers.
 */
GST_START_TEST (rtp_base_depayload_reversed_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x43214321),
      "seq", 0x4242,
      NULL);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x43214321) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 - 1,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (1);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_events_received (3);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* the intent of this test is to push two RTP packets that have reverse sequence
 * numbers that differ significantly. the depayloader will consider RTP packets
 * where the sequence numbers differ by more than 1000 to indicate that the
 * source of the RTP packets has been restarted. therefore it will let both
 * depayloaded buffers through, but the latter buffer marked
 * GST_BUFFER_FLAG_DISCONT to indicate the discontinuity in the stream. the
 * initial events should be unaffected by the reversed buffers.
 */
GST_START_TEST (rtp_base_depayload_old_reversed_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x43214321),
      "seq", 0x4242,
      NULL);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x43214321) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 - 1000,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", TRUE,
      NULL);

  validate_events_received (3);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* a depayloader that has not received any caps event will not be able to
 * process any incoming RTP packet. instead pushing an RTP packet should result
 * in the expected error.
 */
GST_START_TEST (rtp_base_depayload_without_negotiation_test)
{
  State *state;

  state = create_depayloader (NULL, NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer_fails (state, GST_FLOW_NOT_NEGOTIATED,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (0);

  validate_events_received (1);

  validate_event (0, "stream-start",
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* a depayloader that receives the downstream event GstRTPPacketLost should
 * respond by emitting a gap event with the corresponding timestamp and
 * duration. the initial events are unaffected, but are succeeded by the added
 * gap event.
 */
GST_START_TEST (rtp_base_depayload_packet_lost_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  packet_lost (state, 1 * GST_SECOND, GST_SECOND);

  push_rtp_buffer (state,
      "pts", 2 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234) + 2 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 2,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 2 * GST_SECOND,
      "discont", TRUE,
      NULL);

  validate_events_received (4);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  validate_event (3, "gap",
      "timestamp", 1 * GST_SECOND,
      "duration", GST_SECOND,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* a depayloader that receives identical caps events simply ignores the latter
 * events without propagating them downstream.
 */
GST_START_TEST (rtp_base_depayload_repeated_caps_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  reconfigure_caps (state, "application/x-rtp");

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 1,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_events_received (3);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST
/* when a depayloader receives new caps events with npt-start and npt-stop times
 * it should save these timestamps as they should affect the next segment event
 * being pushed by the depayloader. a new segment event is not pushed by the
 * depayloader until a flush_stop event and a succeeding segment event are
 * received. of course the intial event are unaffected, as is the incoming caps
 * event.
 */
GST_START_TEST (rtp_base_depayload_npt_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  reconfigure_caps (state,
      "application/x-rtp, npt-start=(guint64)1234, npt-stop=(guint64)4321");

  flush_pipeline (state);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 1,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_events_received (7);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  validate_event (3, "caps",
      "media-type", "application/x-rtp",
      "npt-start", G_GUINT64_CONSTANT (1234),
      "npt-stop", G_GUINT64_CONSTANT (4321),
      NULL);

  validate_event (4, "flush-start",
      NULL);

  validate_event (5, "flush-stop",
      NULL);

  validate_event (6, "segment",
      "time", G_GUINT64_CONSTANT (1234),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_GUINT64_CONSTANT (4321 - 1234),
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* when a depayloader receives a new caps event with play-scale it should save
 * this rate as it should affect the next segment event being pushed by the
 * depayloader. a new segment event is not pushed by the depayloader until a
 * flush_stop event and a succeeding segment event are received. of course the
 * intial event are unaffected, as is the incoming caps event.
 */
GST_START_TEST (rtp_base_depayload_play_scale_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  reconfigure_caps (state,
      "application/x-rtp, play-scale=(double)2.0");

  flush_pipeline (state);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 1,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_events_received (7);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  validate_event (3, "caps",
      "media-type", "application/x-rtp",
      "play-scale", 2.0,
      NULL);

  validate_event (4, "flush-start",
      NULL);

  validate_event (5, "flush-stop",
      NULL);

  validate_event (6, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      "rate", 1.0,
      "applied-rate", 2.0,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

/* when a depayloader receives a new caps event with play-speed it should save
 * this rate as it should affect the next segment event being pushed by the
 * depayloader. a new segment event is not pushed by the depayloader until a
 * flush_stop event and a succeeding segment event are received. of course the
 * intial event are unaffected, as is the incoming caps event.
 */
GST_START_TEST (rtp_base_depayload_play_speed_test)
{
  State *state;

  state = create_depayloader ("application/x-rtp", NULL);

  set_state (state, GST_STATE_PLAYING);

  push_rtp_buffer (state,
      "pts", 0 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234),
      "seq", 0x4242,
      NULL);

  reconfigure_caps (state,
      "application/x-rtp, play-speed=(double)2.0");

  flush_pipeline (state);

  push_rtp_buffer (state,
      "pts", 1 * GST_SECOND,
      "rtptime", G_GUINT64_CONSTANT (0x1234) + 1 * DEFAULT_CLOCK_RATE,
      "seq", 0x4242 + 1,
      NULL);

  set_state (state, GST_STATE_NULL);

  validate_buffers_received (2);

  validate_buffer (0,
      "pts", 0 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_buffer (1,
      "pts", 1 * GST_SECOND,
      "discont", FALSE,
      NULL);

  validate_events_received (7);

  validate_event (0, "stream-start",
      NULL);

  validate_event (1, "caps",
      "media-type", "application/x-rtp",
      NULL);

  validate_event (2, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      NULL);

  validate_event (3, "caps",
      "media-type", "application/x-rtp",
      "play-speed", 2.0,
      NULL);

  validate_event (4, "flush-start",
      NULL);

  validate_event (5, "flush-stop",
      NULL);

  validate_event (6, "segment",
      "time", G_GUINT64_CONSTANT (0),
      "start", G_GUINT64_CONSTANT (0),
      "stop", G_MAXUINT64,
      "rate", 2.0,
      "applied-rate", 1.0,
      NULL);

  destroy_depayloader (state);
}

GST_END_TEST

static Suite *
rtp_basepayloading_suite (void)
{
  Suite *s = suite_create ("rtp_base_depayloading_test");
  TCase *tc_chain = tcase_create ("depayloading tests");

  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, rtp_base_depayload_buffer_test);

  tcase_add_test (tc_chain, rtp_base_depayload_invalid_rtp_packet_test);
  tcase_add_test (tc_chain, rtp_base_depayload_with_gap_test);
  tcase_add_test (tc_chain, rtp_base_depayload_reversed_test);
  tcase_add_test (tc_chain, rtp_base_depayload_old_reversed_test);

  tcase_add_test (tc_chain, rtp_base_depayload_without_negotiation_test);

  tcase_add_test (tc_chain, rtp_base_depayload_packet_lost_test);

  tcase_add_test (tc_chain, rtp_base_depayload_repeated_caps_test);
  tcase_add_test (tc_chain, rtp_base_depayload_npt_test);
  tcase_add_test (tc_chain, rtp_base_depayload_play_scale_test);
  tcase_add_test (tc_chain, rtp_base_depayload_play_speed_test);

  return s;
}

GST_CHECK_MAIN (rtp_basepayloading)
