/*
 * gstrtponviftimestamp.h
 *
 * Copyright (C) 2014 Axis Communications AB
 *  Author: Guillaume Desmottes <guillaume.desmottes@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtponviftimestamp.h"

#define DEFAULT_NTP_OFFSET GST_CLOCK_TIME_NONE
#define DEFAULT_CSEQ 0
#define DEFAULT_SET_E_BIT FALSE

GST_DEBUG_CATEGORY_STATIC (rtponviftimestamp_debug);
#define GST_CAT_DEFAULT (rtponviftimestamp_debug)

static GstFlowReturn gst_rtp_onvif_timestamp_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_rtp_onvif_timestamp_chain_list (GstPad * pad,
    GstObject * parent, GstBufferList * list);

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

enum
{
  PROP_0,
  PROP_NTP_OFFSET,
  PROP_CSEQ,
  PROP_SET_E_BIT,
};

/*static guint gst_rtp_onvif_timestamp_signals[LAST_SIGNAL] = { 0 }; */

G_DEFINE_TYPE (GstRtpOnvifTimestamp, gst_rtp_onvif_timestamp, GST_TYPE_ELEMENT);

static void
gst_rtp_onvif_timestamp_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (object);

  switch (prop_id) {
    case PROP_NTP_OFFSET:
      g_value_set_uint64 (value, self->prop_ntp_offset);
      break;
    case PROP_CSEQ:
      g_value_set_uint (value, self->prop_cseq);
      break;
    case PROP_SET_E_BIT:
      g_value_set_boolean (value, self->prop_set_e_bit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_onvif_timestamp_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (object);

  switch (prop_id) {
    case PROP_NTP_OFFSET:
      self->prop_ntp_offset = g_value_get_uint64 (value);
      break;
    case PROP_CSEQ:
      self->prop_cseq = g_value_get_uint (value);
      break;
    case PROP_SET_E_BIT:
      self->prop_set_e_bit = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_onvif_timestamp_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_rtp_onvif_timestamp_parent_class)->change_state
      (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (GST_CLOCK_TIME_IS_VALID (self->prop_ntp_offset))
        self->ntp_offset = self->prop_ntp_offset;
      else
        self->ntp_offset = GST_CLOCK_TIME_NONE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!GST_CLOCK_TIME_IS_VALID (self->prop_ntp_offset) &&
          GST_ELEMENT_CLOCK (element) == NULL) {
        GST_ELEMENT_ERROR (element, CORE, CLOCK, ("Missing NTP offset"),
            ("Set the \"ntp-offset\" property to,"
                " can't guess it without a clock on the pipeline."));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_rtp_onvif_timestamp_finalize (GObject * object)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (object);

  if (self->buffer)
    gst_buffer_unref (self->buffer);
  if (self->list)
    gst_buffer_list_unref (self->list);

  G_OBJECT_CLASS (gst_rtp_onvif_timestamp_parent_class)->finalize (object);
}

static void
gst_rtp_onvif_timestamp_class_init (GstRtpOnvifTimestampClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->get_property = gst_rtp_onvif_timestamp_get_property;
  gobject_class->set_property = gst_rtp_onvif_timestamp_set_property;
  gobject_class->finalize = gst_rtp_onvif_timestamp_finalize;

  g_object_class_install_property (gobject_class, PROP_NTP_OFFSET,
      g_param_spec_uint64 ("ntp-offset", "NTP offset",
          "Offset between the pipeline running time and the absolute UTC time, "
          "in nano-seconds since 1900 (-1 for automatic computation)",
          0, G_MAXUINT64,
          DEFAULT_NTP_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CSEQ,
      g_param_spec_uint ("cseq", "CSeq",
          "The RTSP CSeq which initiated the playback",
          0, G_MAXUINT32,
          DEFAULT_CSEQ, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SET_E_BIT,
      g_param_spec_boolean ("set-e-bit", "Set 'E' bit",
          "If the element should set the 'E' bit as defined in the ONVIF RTP "
          "extension. This increases latency by one packet",
          DEFAULT_SET_E_BIT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* register pads */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "ONVIF NTP timestamps RTP extension", "Effect/RTP",
      "Add absolute timestamps and flags of recorded data in a playback "
      "session", "Guillaume Desmottes <guillaume.desmottes@collabora.com>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_onvif_timestamp_change_state);

  GST_DEBUG_CATEGORY_INIT (rtponviftimestamp_debug, "rtponviftimestamp",
      0, "ONVIF NTP timestamps RTP extension");
}

static GstFlowReturn handle_and_push_buffer (GstRtpOnvifTimestamp * self,
    GstBuffer * buf, gboolean end_contiguous);
static GstFlowReturn handle_and_push_buffer_list (GstRtpOnvifTimestamp * self,
    GstBufferList * list, gboolean end_contiguous);

static gboolean
gst_rtp_onvif_timestamp_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (parent);

  GST_DEBUG_OBJECT (pad, "handling event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &self->segment);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_EOS:
      /* Push pending buffers, if any */
      if (self->buffer) {
        handle_and_push_buffer (self, self->buffer, TRUE);
        self->buffer = NULL;
      }
      if (self->list) {
        handle_and_push_buffer_list (self, self->list, TRUE);
        self->list = NULL;
      }
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static void
gst_rtp_onvif_timestamp_init (GstRtpOnvifTimestamp * self)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sink_template_factory, "sink");
  gst_pad_set_chain_function (self->sinkpad, gst_rtp_onvif_timestamp_chain);
  gst_pad_set_chain_list_function (self->sinkpad,
      gst_rtp_onvif_timestamp_chain_list);
  gst_pad_set_event_function (self->sinkpad,
      gst_rtp_onvif_timestamp_sink_event);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);
  GST_PAD_SET_PROXY_CAPS (self->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (self->sinkpad);

  self->srcpad =
      gst_pad_new_from_static_template (&src_template_factory, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->prop_ntp_offset = DEFAULT_NTP_OFFSET;
  self->prop_set_e_bit = DEFAULT_SET_E_BIT;

  self->buffer = NULL;
  self->list = NULL;

  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
}

#define EXTENSION_ID 0xABAC
#define EXTENSION_SIZE 3

static gboolean
handle_buffer (GstRtpOnvifTimestamp * self, GstBuffer * buf,
    gboolean end_contiguous)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *data;
  guint16 bits;
  guint wordlen;
  guint64 time;
  guint8 field = 0;

  if (!GST_CLOCK_TIME_IS_VALID (self->ntp_offset)) {
    GstClock *clock = gst_element_get_clock (GST_ELEMENT (self));

    if (clock) {
      GstClockTime clock_time = gst_clock_get_time (clock);
      guint64 real_time = g_get_real_time ();
      GstClockTime running_time = clock_time -
          gst_element_get_base_time (GST_ELEMENT (self));

      /* convert microseconds to nanoseconds */
      real_time *= 1000;

      /* add constant to convert from 1970 based time to 1900 based time */
      real_time += (G_GUINT64_CONSTANT (2208988800) * GST_SECOND);

      self->ntp_offset = real_time - running_time;

      gst_object_unref (clock);
    } else {
      /* Received a buffer in PAUSED, so we can't guess the match
       * between the running time and the NTP clock yet.
       */
      return TRUE;
    }
  }

  if (self->segment.format != GST_FORMAT_TIME) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED,
        ("did not receive a time segment yet"), (NULL));
    return FALSE;
  }

  if (!gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp)) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED,
        ("Failed to map RTP buffer"), (NULL));
    return FALSE;
  }

  if (!gst_rtp_buffer_set_extension_data (&rtp, EXTENSION_ID, EXTENSION_SIZE)) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Failed to set extension data"),
        (NULL));
    gst_rtp_buffer_unmap (&rtp);
    return FALSE;
  }

  if (!gst_rtp_buffer_get_extension_data (&rtp, &bits, (gpointer) & data,
          &wordlen)) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Failed to get extension data"),
        (NULL));
    gst_rtp_buffer_unmap (&rtp);
    return FALSE;
  }

  /* NTP timestamp */
  if (GST_BUFFER_DTS_IS_VALID (buf)) {
    time = gst_segment_to_running_time (&self->segment, GST_FORMAT_TIME,
        GST_BUFFER_DTS (buf));
  } else if (GST_BUFFER_PTS_IS_VALID (buf)) {
    time = gst_segment_to_running_time (&self->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (buf));
  } else {
    GST_ERROR_OBJECT (self,
        "Buffer doesn't contain any valid DTS or PTS timestamp");
    goto done;
  }

  if (time == GST_CLOCK_TIME_NONE) {
    GST_ERROR_OBJECT (self, "Failed to get running time");
    goto done;
  }

  /* add the offset (in seconds) */
  time += self->ntp_offset;

  /* convert to NTP time. upper 32 bits should contain the seconds
   * and the lower 32 bits, the fractions of a second. */
  time = gst_util_uint64_scale (time, (G_GINT64_CONSTANT (1) << 32),
      GST_SECOND);

  GST_DEBUG_OBJECT (self, "timestamp: %" G_GUINT64_FORMAT, time);

  GST_WRITE_UINT64_BE (data, time);

  /* The next byte is composed of: C E D mbz (5 bits) */

  /* Set C if the buffer does *not* have the DELTA_UNIT flag as it means
   * that's a key frame (or 'clean point'). */
  if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
    GST_DEBUG_OBJECT (self, "set C flag");
    field |= (1 << 7);
  }

  /* Set E if the next buffer has DISCONT */
  if (end_contiguous) {
    GST_DEBUG_OBJECT (self, "set E flag");
    field |= (1 << 6);
  }

  /* Set D if the buffer has the DISCONT flag */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (self, "set D flag");
    field |= (1 << 5);
  }

  GST_WRITE_UINT8 (data + 8, field);

  /* CSeq (low-order byte) */
  GST_WRITE_UINT8 (data + 9, (guchar) self->prop_cseq);

  memset (data + 10, 0, 3);

done:
  gst_rtp_buffer_unmap (&rtp);
  return TRUE;
}

/* @buf: (transfer all) */
static GstFlowReturn
handle_and_push_buffer (GstRtpOnvifTimestamp * self, GstBuffer * buf,
    gboolean end_contiguous)
{
  if (!handle_buffer (self, buf, end_contiguous)) {
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  return gst_pad_push (self->srcpad, buf);
}

static GstFlowReturn
gst_rtp_onvif_timestamp_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (parent);
  GstFlowReturn result = GST_FLOW_OK;

  if (!self->prop_set_e_bit) {
    /* Modify and push this buffer right away */
    return handle_and_push_buffer (self, buf, FALSE);
  }

  /* We have to wait for the *next* buffer before pushing this one */

  if (self->buffer) {
    /* push the *previous* buffer received */
    result = handle_and_push_buffer (self, self->buffer,
        GST_BUFFER_IS_DISCONT (buf));
  }

  /* Transfer ownership */
  self->buffer = buf;
  return result;
}

/* @buf: (transfer all) */
static GstFlowReturn
handle_and_push_buffer_list (GstRtpOnvifTimestamp * self,
    GstBufferList * list, gboolean end_contiguous)
{
  GstBuffer *buf;

  /* Set the extension on the *first* buffer */
  buf = gst_buffer_list_get (list, 0);
  if (!handle_buffer (self, buf, end_contiguous)) {
    gst_buffer_list_unref (list);
    return GST_FLOW_ERROR;
  }

  return gst_pad_push_list (self->srcpad, list);
}

/* gst_pad_chain_list_default() refs the buffer when passing it to the chain
 * function, making it not writable. We implement our own chain_list function
 * to avoid having to copy each buffer. */
static GstFlowReturn
gst_rtp_onvif_timestamp_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * list)
{
  GstRtpOnvifTimestamp *self = GST_RTP_ONVIF_TIMESTAMP (parent);
  GstFlowReturn result = GST_FLOW_OK;
  GstBuffer *buf;

  if (!self->prop_set_e_bit) {
    return handle_and_push_buffer_list (self, list, FALSE);
  }

  /* We have to wait for the *next* list before pushing this one */

  if (self->list) {
    /* push the *previous* list received */
    buf = gst_buffer_list_get (list, 0);

    result = handle_and_push_buffer_list (self, self->list,
        GST_BUFFER_IS_DISCONT (buf));
  }

  /* Transfer ownership */
  self->list = list;
  return result;
}
