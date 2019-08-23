/* GStreamer
 * Copyright (C) 2018 Matthew Waters <matthew@centricular.com>
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

/**
 * SECTION:gstwebrtc-datachannel
 * @short_description: RTCDataChannel object
 * @title: GstWebRTCDataChannel
 * @see_also: #GstWebRTCRTPTransceiver
 *
 * <http://w3c.github.io/webrtc-pc/#dom-rtcsctptransport>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "webrtcdatachannel.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <gst/sctp/sctpreceivemeta.h>
#include <gst/sctp/sctpsendmeta.h>

#include "gstwebrtcbin.h"
#include "utils.h"

#define GST_CAT_DEFAULT gst_webrtc_data_channel_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_webrtc_data_channel_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCDataChannel, gst_webrtc_data_channel,
    G_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (gst_webrtc_data_channel_debug,
        "webrtcdatachannel", 0, "webrtcdatachannel"););

#define CHANNEL_LOCK(channel) g_mutex_lock(&channel->lock)
#define CHANNEL_UNLOCK(channel) g_mutex_unlock(&channel->lock)

enum
{
  SIGNAL_0,
  SIGNAL_ON_OPEN,
  SIGNAL_ON_CLOSE,
  SIGNAL_ON_ERROR,
  SIGNAL_ON_MESSAGE_DATA,
  SIGNAL_ON_MESSAGE_STRING,
  SIGNAL_ON_BUFFERED_AMOUNT_LOW,
  SIGNAL_SEND_DATA,
  SIGNAL_SEND_STRING,
  SIGNAL_CLOSE,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_LABEL,
  PROP_ORDERED,
  PROP_MAX_PACKET_LIFETIME,
  PROP_MAX_RETRANSMITS,
  PROP_PROTOCOL,
  PROP_NEGOTIATED,
  PROP_ID,
  PROP_PRIORITY,
  PROP_READY_STATE,
  PROP_BUFFERED_AMOUNT,
  PROP_BUFFERED_AMOUNT_LOW_THRESHOLD,
};

static guint gst_webrtc_data_channel_signals[LAST_SIGNAL] = { 0 };

typedef enum
{
  DATA_CHANNEL_PPID_WEBRTC_CONTROL = 50,
  DATA_CHANNEL_PPID_WEBRTC_STRING = 51,
  DATA_CHANNEL_PPID_WEBRTC_BINARY_PARTIAL = 52, /* deprecated */
  DATA_CHANNEL_PPID_WEBRTC_BINARY = 53,
  DATA_CHANNEL_PPID_WEBRTC_STRING_PARTIAL = 54, /* deprecated */
  DATA_CHANNEL_PPID_WEBRTC_BINARY_EMPTY = 56,
  DATA_CHANNEL_PPID_WEBRTC_STRING_EMPTY = 57,
} DataChannelPPID;

typedef enum
{
  CHANNEL_TYPE_RELIABLE = 0x00,
  CHANNEL_TYPE_RELIABLE_UNORDERED = 0x80,
  CHANNEL_TYPE_PARTIAL_RELIABLE_REXMIT = 0x01,
  CHANNEL_TYPE_PARTIAL_RELIABLE_REXMIT_UNORDERED = 0x81,
  CHANNEL_TYPE_PARTIAL_RELIABLE_TIMED = 0x02,
  CHANNEL_TYPE_PARTIAL_RELIABLE_TIMED_UNORDERED = 0x82,
} DataChannelReliabilityType;

typedef enum
{
  CHANNEL_MESSAGE_ACK = 0x02,
  CHANNEL_MESSAGE_OPEN = 0x03,
} DataChannelMessage;

static guint16
priority_type_to_uint (GstWebRTCPriorityType pri)
{
  switch (pri) {
    case GST_WEBRTC_PRIORITY_TYPE_VERY_LOW:
      return 64;
    case GST_WEBRTC_PRIORITY_TYPE_LOW:
      return 192;
    case GST_WEBRTC_PRIORITY_TYPE_MEDIUM:
      return 384;
    case GST_WEBRTC_PRIORITY_TYPE_HIGH:
      return 768;
  }
  g_assert_not_reached ();
  return 0;
}

static GstWebRTCPriorityType
priority_uint_to_type (guint16 val)
{
  if (val <= 128)
    return GST_WEBRTC_PRIORITY_TYPE_VERY_LOW;
  if (val <= 256)
    return GST_WEBRTC_PRIORITY_TYPE_LOW;
  if (val <= 512)
    return GST_WEBRTC_PRIORITY_TYPE_MEDIUM;
  return GST_WEBRTC_PRIORITY_TYPE_HIGH;
}

static GstBuffer *
construct_open_packet (GstWebRTCDataChannel * channel)
{
  GstByteWriter w;
  gsize label_len = strlen (channel->label);
  gsize proto_len = strlen (channel->protocol);
  gsize size = 12 + label_len + proto_len;
  DataChannelReliabilityType reliability = 0;
  guint32 reliability_param = 0;
  guint16 priority;
  GstBuffer *buf;

/*
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |  Message Type |  Channel Type |            Priority           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                    Reliability Parameter                      |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |         Label Length          |       Protocol Length         |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   \                                                               /
 *   |                             Label                             |
 *   /                                                               \
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   \                                                               /
 *   |                            Protocol                           |
 *   /                                                               \
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

  gst_byte_writer_init_with_size (&w, size, FALSE);

  if (!gst_byte_writer_put_uint8 (&w, (guint8) CHANNEL_MESSAGE_OPEN))
    g_return_val_if_reached (NULL);

  if (!channel->ordered)
    reliability |= 0x80;
  if (channel->max_retransmits != -1) {
    reliability |= 0x01;
    reliability_param = channel->max_retransmits;
  }
  if (channel->max_packet_lifetime != -1) {
    reliability |= 0x02;
    reliability_param = channel->max_packet_lifetime;
  }

  priority = priority_type_to_uint (channel->priority);

  if (!gst_byte_writer_put_uint8 (&w, (guint8) reliability))
    g_return_val_if_reached (NULL);
  if (!gst_byte_writer_put_uint16_be (&w, (guint16) priority))
    g_return_val_if_reached (NULL);
  if (!gst_byte_writer_put_uint32_be (&w, (guint32) reliability_param))
    g_return_val_if_reached (NULL);
  if (!gst_byte_writer_put_uint16_be (&w, (guint16) label_len))
    g_return_val_if_reached (NULL);
  if (!gst_byte_writer_put_uint16_be (&w, (guint16) proto_len))
    g_return_val_if_reached (NULL);
  if (!gst_byte_writer_put_data (&w, (guint8 *) channel->label, label_len))
    g_return_val_if_reached (NULL);
  if (!gst_byte_writer_put_data (&w, (guint8 *) channel->protocol, proto_len))
    g_return_val_if_reached (NULL);

  buf = gst_byte_writer_reset_and_get_buffer (&w);

  /* send reliable and ordered */
  gst_sctp_buffer_add_send_meta (buf, DATA_CHANNEL_PPID_WEBRTC_CONTROL, TRUE,
      GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE, 0);

  return buf;
}

static GstBuffer *
construct_ack_packet (GstWebRTCDataChannel * channel)
{
  GstByteWriter w;
  GstBuffer *buf;

/*
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |  Message Type |
 *   +-+-+-+-+-+-+-+-+
 */

  gst_byte_writer_init_with_size (&w, 1, FALSE);

  if (!gst_byte_writer_put_uint8 (&w, (guint8) CHANNEL_MESSAGE_ACK))
    g_return_val_if_reached (NULL);

  buf = gst_byte_writer_reset_and_get_buffer (&w);

  /* send reliable and ordered */
  gst_sctp_buffer_add_send_meta (buf, DATA_CHANNEL_PPID_WEBRTC_CONTROL, TRUE,
      GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE, 0);

  return buf;
}

typedef void (*ChannelTask) (GstWebRTCDataChannel * channel,
    gpointer user_data);

struct task
{
  GstWebRTCDataChannel *channel;
  ChannelTask func;
  gpointer user_data;
  GDestroyNotify notify;
};

static void
_execute_task (GstWebRTCBin * webrtc, struct task *task)
{
  if (task->func)
    task->func (task->channel, task->user_data);
}

static void
_free_task (struct task *task)
{
  gst_object_unref (task->channel);

  if (task->notify)
    task->notify (task->user_data);
  g_free (task);
}

static void
_channel_enqueue_task (GstWebRTCDataChannel * channel, ChannelTask func,
    gpointer user_data, GDestroyNotify notify)
{
  struct task *task = g_new0 (struct task, 1);

  task->channel = gst_object_ref (channel);
  task->func = func;
  task->user_data = user_data;
  task->notify = notify;

  gst_webrtc_bin_enqueue_task (channel->webrtcbin,
      (GstWebRTCBinFunc) _execute_task, task, (GDestroyNotify) _free_task);
}

static void
_channel_store_error (GstWebRTCDataChannel * channel, GError * error)
{
  CHANNEL_LOCK (channel);
  if (error) {
    GST_WARNING_OBJECT (channel, "Error: %s",
        error ? error->message : "Unknown");
    if (!channel->stored_error)
      channel->stored_error = error;
    else
      g_clear_error (&error);
  }
  CHANNEL_UNLOCK (channel);
}

static void
_maybe_emit_on_error (GstWebRTCDataChannel * channel, GError * error)
{
  if (error) {
    GST_WARNING_OBJECT (channel, "error thrown");
    g_signal_emit (channel, gst_webrtc_data_channel_signals[SIGNAL_ON_ERROR], 0,
        error);
  }
}

static void
_emit_on_open (GstWebRTCDataChannel * channel, gpointer user_data)
{
  CHANNEL_LOCK (channel);
  if (channel->ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_CLOSING ||
      channel->ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED) {
    CHANNEL_UNLOCK (channel);
    return;
  }

  if (channel->ready_state != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
    channel->ready_state = GST_WEBRTC_DATA_CHANNEL_STATE_OPEN;
    CHANNEL_UNLOCK (channel);
    g_object_notify (G_OBJECT (channel), "ready-state");

    GST_INFO_OBJECT (channel, "We are open and ready for data!");
    g_signal_emit (channel, gst_webrtc_data_channel_signals[SIGNAL_ON_OPEN], 0,
        NULL);
  } else {
    CHANNEL_UNLOCK (channel);
  }
}

static void
_transport_closed_unlocked (GstWebRTCDataChannel * channel)
{
  GError *error;

  if (channel->ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED)
    return;

  channel->ready_state = GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED;

  error = channel->stored_error;
  channel->stored_error = NULL;
  CHANNEL_UNLOCK (channel);

  g_object_notify (G_OBJECT (channel), "ready-state");
  GST_INFO_OBJECT (channel, "We are closed for data");

  _maybe_emit_on_error (channel, error);

  g_signal_emit (channel, gst_webrtc_data_channel_signals[SIGNAL_ON_CLOSE], 0,
      NULL);
  CHANNEL_LOCK (channel);
}

static void
_transport_closed (GstWebRTCDataChannel * channel, gpointer user_data)
{
  CHANNEL_LOCK (channel);
  _transport_closed_unlocked (channel);
  CHANNEL_UNLOCK (channel);
}

static void
_close_sctp_stream (GstWebRTCDataChannel * channel, gpointer user_data)
{
  GstPad *pad, *peer;

  pad = gst_element_get_static_pad (channel->appsrc, "src");
  peer = gst_pad_get_peer (pad);
  gst_object_unref (pad);

  if (peer) {
    GstElement *sctpenc = gst_pad_get_parent_element (peer);

    if (sctpenc) {
      gst_element_release_request_pad (sctpenc, peer);
      gst_object_unref (sctpenc);
    }
    gst_object_unref (peer);
  }

  _transport_closed (channel, NULL);
}

static void
_close_procedure (GstWebRTCDataChannel * channel, gpointer user_data)
{
  /* https://www.w3.org/TR/webrtc/#data-transport-closing-procedure */
  CHANNEL_LOCK (channel);
  if (channel->ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED
      || channel->ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_CLOSING) {
    CHANNEL_UNLOCK (channel);
    return;
  }
  channel->ready_state = GST_WEBRTC_DATA_CHANNEL_STATE_CLOSING;
  CHANNEL_UNLOCK (channel);
  g_object_notify (G_OBJECT (channel), "ready-state");

  CHANNEL_LOCK (channel);
  if (channel->buffered_amount <= 0) {
    _channel_enqueue_task (channel, (ChannelTask) _close_sctp_stream,
        NULL, NULL);
  }

  CHANNEL_UNLOCK (channel);
}

static void
_on_sctp_reset_stream (GstWebRTCSCTPTransport * sctp, guint stream_id,
    GstWebRTCDataChannel * channel)
{
  if (channel->id == stream_id)
    _channel_enqueue_task (channel, (ChannelTask) _transport_closed,
        GUINT_TO_POINTER (stream_id), NULL);
}

static void
gst_webrtc_data_channel_close (GstWebRTCDataChannel * channel)
{
  _close_procedure (channel, NULL);
}

static GstFlowReturn
_parse_control_packet (GstWebRTCDataChannel * channel, guint8 * data,
    gsize size, GError ** error)
{
  GstByteReader r;
  guint8 message_type;

  if (!data)
    g_return_val_if_reached (GST_FLOW_ERROR);
  if (size < 1)
    g_return_val_if_reached (GST_FLOW_ERROR);

  gst_byte_reader_init (&r, data, size);

  if (!gst_byte_reader_get_uint8 (&r, &message_type))
    g_return_val_if_reached (GST_FLOW_ERROR);

  if (message_type == CHANNEL_MESSAGE_ACK) {
    /* all good */
    GST_INFO_OBJECT (channel, "Received channel ack");
    return GST_FLOW_OK;
  } else if (message_type == CHANNEL_MESSAGE_OPEN) {
    guint8 reliability;
    guint32 reliability_param;
    guint16 priority, label_len, proto_len;
    const guint8 *src;
    gchar *label, *proto;
    GstBuffer *buffer;
    GstFlowReturn ret;

    GST_INFO_OBJECT (channel, "Received channel open");

    if (channel->negotiated) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
          "Data channel was signalled as negotiated already");
      g_return_val_if_reached (GST_FLOW_ERROR);
    }

    if (channel->opened)
      return GST_FLOW_OK;

    if (!gst_byte_reader_get_uint8 (&r, &reliability))
      goto parse_error;
    if (!gst_byte_reader_get_uint16_be (&r, &priority))
      goto parse_error;
    if (!gst_byte_reader_get_uint32_be (&r, &reliability_param))
      goto parse_error;
    if (!gst_byte_reader_get_uint16_be (&r, &label_len))
      goto parse_error;
    if (!gst_byte_reader_get_uint16_be (&r, &proto_len))
      goto parse_error;

    label = g_new0 (gchar, (gsize) label_len + 1);
    proto = g_new0 (gchar, (gsize) proto_len + 1);

    if (!gst_byte_reader_get_data (&r, label_len, &src))
      goto parse_error;
    memcpy (label, src, label_len);
    label[label_len] = '\0';
    if (!gst_byte_reader_get_data (&r, proto_len, &src))
      goto parse_error;
    memcpy (proto, src, proto_len);
    proto[proto_len] = '\0';

    channel->label = label;
    channel->protocol = proto;
    channel->priority = priority_uint_to_type (priority);
    channel->ordered = !(reliability & 0x80);
    if (reliability & 0x01) {
      channel->max_retransmits = reliability_param;
      channel->max_packet_lifetime = -1;
    } else if (reliability & 0x02) {
      channel->max_retransmits = -1;
      channel->max_packet_lifetime = reliability_param;
    } else {
      channel->max_retransmits = -1;
      channel->max_packet_lifetime = -1;
    }
    channel->opened = TRUE;

    GST_INFO_OBJECT (channel, "Received channel open for SCTP stream %i "
        "label %s protocol %s ordered %s", channel->id, channel->label,
        channel->protocol, channel->ordered ? "true" : "false");

    _channel_enqueue_task (channel, (ChannelTask) _emit_on_open, NULL, NULL);

    GST_INFO_OBJECT (channel, "Sending channel ack");
    buffer = construct_ack_packet (channel);

    CHANNEL_LOCK (channel);
    channel->buffered_amount += gst_buffer_get_size (buffer);
    CHANNEL_UNLOCK (channel);

    ret = gst_app_src_push_buffer (GST_APP_SRC (channel->appsrc), buffer);
    if (ret != GST_FLOW_OK) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
          "Could not send ack packet");
    }
    return ret;
  } else {
    g_set_error (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
        "Unknown message type in control protocol");
    return GST_FLOW_ERROR;
  }

parse_error:
  {
    g_set_error (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE, "Failed to parse packet");
    g_return_val_if_reached (GST_FLOW_ERROR);
  }
}

static void
on_sink_eos (GstAppSink * sink, gpointer user_data)
{
}

struct map_info
{
  GstBuffer *buffer;
  GstMapInfo map_info;
};

static void
buffer_unmap_and_unref (struct map_info *info)
{
  gst_buffer_unmap (info->buffer, &info->map_info);
  gst_buffer_unref (info->buffer);
  g_free (info);
}

static void
_emit_have_data (GstWebRTCDataChannel * channel, GBytes * data)
{
  GST_LOG_OBJECT (channel, "Have data %p", data);
  g_signal_emit (channel,
      gst_webrtc_data_channel_signals[SIGNAL_ON_MESSAGE_DATA], 0, data);
}

static void
_emit_have_string (GstWebRTCDataChannel * channel, gchar * str)
{
  GST_LOG_OBJECT (channel, "Have string %p", str);
  g_signal_emit (channel,
      gst_webrtc_data_channel_signals[SIGNAL_ON_MESSAGE_STRING], 0, str);
}

static GstFlowReturn
_data_channel_have_sample (GstWebRTCDataChannel * channel, GstSample * sample,
    GError ** error)
{
  GstSctpReceiveMeta *receive;
  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (channel, "Received sample %" GST_PTR_FORMAT, sample);

  g_return_val_if_fail (channel->sctp_transport != NULL, GST_FLOW_ERROR);

  buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE, "No buffer to handle");
    return GST_FLOW_ERROR;
  }
  receive = gst_sctp_buffer_get_receive_meta (buffer);
  if (!receive) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
        "No SCTP Receive meta on the buffer");
    return GST_FLOW_ERROR;
  }

  switch (receive->ppid) {
    case DATA_CHANNEL_PPID_WEBRTC_CONTROL:{
      GstMapInfo info = GST_MAP_INFO_INIT;
      if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR,
            GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
            "Failed to map received buffer");
        ret = GST_FLOW_ERROR;
      } else {
        ret = _parse_control_packet (channel, info.data, info.size, error);
      }
      break;
    }
    case DATA_CHANNEL_PPID_WEBRTC_STRING:
    case DATA_CHANNEL_PPID_WEBRTC_STRING_PARTIAL:{
      GstMapInfo info = GST_MAP_INFO_INIT;
      if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR,
            GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
            "Failed to map received buffer");
        ret = GST_FLOW_ERROR;
      } else {
        gchar *str = g_strndup ((gchar *) info.data, info.size);
        _channel_enqueue_task (channel, (ChannelTask) _emit_have_string, str,
            g_free);
      }
      break;
    }
    case DATA_CHANNEL_PPID_WEBRTC_BINARY:
    case DATA_CHANNEL_PPID_WEBRTC_BINARY_PARTIAL:{
      struct map_info *info = g_new0 (struct map_info, 1);
      if (!gst_buffer_map (buffer, &info->map_info, GST_MAP_READ)) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR,
            GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
            "Failed to map received buffer");
        ret = GST_FLOW_ERROR;
      } else {
        GBytes *data = g_bytes_new_with_free_func (info->map_info.data,
            info->map_info.size, (GDestroyNotify) buffer_unmap_and_unref, info);
        info->buffer = gst_buffer_ref (buffer);
        _channel_enqueue_task (channel, (ChannelTask) _emit_have_data, data,
            (GDestroyNotify) g_bytes_unref);
      }
      break;
    }
    case DATA_CHANNEL_PPID_WEBRTC_BINARY_EMPTY:
      _channel_enqueue_task (channel, (ChannelTask) _emit_have_data, NULL,
          NULL);
      break;
    case DATA_CHANNEL_PPID_WEBRTC_STRING_EMPTY:
      _channel_enqueue_task (channel, (ChannelTask) _emit_have_string, NULL,
          NULL);
      break;
    default:
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
          "Unknown SCTP PPID %u received", receive->ppid);
      ret = GST_FLOW_ERROR;
      break;
  }

  return ret;
}

static GstFlowReturn
on_sink_preroll (GstAppSink * sink, gpointer user_data)
{
  GstWebRTCDataChannel *channel = user_data;
  GstSample *sample = gst_app_sink_pull_preroll (sink);
  GstFlowReturn ret;

  if (sample) {
    /* This sample also seems to be provided by the sample callback
       ret = _data_channel_have_sample (channel, sample); */
    ret = GST_FLOW_OK;
    gst_sample_unref (sample);
  } else if (gst_app_sink_is_eos (sink)) {
    ret = GST_FLOW_EOS;
  } else {
    ret = GST_FLOW_ERROR;
  }

  if (ret != GST_FLOW_OK) {
    _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL, NULL);
  }

  return ret;
}

static GstFlowReturn
on_sink_sample (GstAppSink * sink, gpointer user_data)
{
  GstWebRTCDataChannel *channel = user_data;
  GstSample *sample = gst_app_sink_pull_sample (sink);
  GstFlowReturn ret;
  GError *error = NULL;

  if (sample) {
    ret = _data_channel_have_sample (channel, sample, &error);
    gst_sample_unref (sample);
  } else if (gst_app_sink_is_eos (sink)) {
    ret = GST_FLOW_EOS;
  } else {
    ret = GST_FLOW_ERROR;
  }

  if (error)
    _channel_store_error (channel, error);

  if (ret != GST_FLOW_OK) {
    _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL, NULL);
  }

  return ret;
}

static GstAppSinkCallbacks sink_callbacks = {
  on_sink_eos,
  on_sink_preroll,
  on_sink_sample,
};

void
gst_webrtc_data_channel_start_negotiation (GstWebRTCDataChannel * channel)
{
  GstBuffer *buffer;

  g_return_if_fail (!channel->negotiated);
  g_return_if_fail (channel->id != -1);
  g_return_if_fail (channel->sctp_transport != NULL);

  buffer = construct_open_packet (channel);

  GST_INFO_OBJECT (channel, "Sending channel open for SCTP stream %i "
      "label %s protocol %s ordered %s", channel->id, channel->label,
      channel->protocol, channel->ordered ? "true" : "false");

  CHANNEL_LOCK (channel);
  channel->buffered_amount += gst_buffer_get_size (buffer);
  CHANNEL_UNLOCK (channel);

  if (gst_app_src_push_buffer (GST_APP_SRC (channel->appsrc),
          buffer) == GST_FLOW_OK) {
    channel->opened = TRUE;
    _channel_enqueue_task (channel, (ChannelTask) _emit_on_open, NULL, NULL);
  } else {
    GError *error = NULL;
    g_set_error (&error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
        "Failed to send DCEP open packet");
    _channel_store_error (channel, error);
    _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL, NULL);
  }
}

static void
_get_sctp_reliability (GstWebRTCDataChannel * channel,
    GstSctpSendMetaPartiallyReliability * reliability, guint * rel_param)
{
  if (channel->max_retransmits != -1) {
    *reliability = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_RTX;
    *rel_param = channel->max_retransmits;
  } else if (channel->max_packet_lifetime != -1) {
    *reliability = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_TTL;
    *rel_param = channel->max_packet_lifetime;
  } else {
    *reliability = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE;
    *rel_param = 0;
  }
}

static gboolean
_is_within_max_message_size (GstWebRTCDataChannel * channel, gsize size)
{
  return size <= channel->sctp_transport->max_message_size;
}

static void
gst_webrtc_data_channel_send_data (GstWebRTCDataChannel * channel,
    GBytes * bytes)
{
  GstSctpSendMetaPartiallyReliability reliability;
  guint rel_param;
  guint32 ppid;
  GstBuffer *buffer;
  GstFlowReturn ret;

  if (!bytes) {
    buffer = gst_buffer_new ();
    ppid = DATA_CHANNEL_PPID_WEBRTC_BINARY_EMPTY;
  } else {
    gsize size;
    guint8 *data;

    data = (guint8 *) g_bytes_get_data (bytes, &size);
    g_return_if_fail (data != NULL);
    if (!_is_within_max_message_size (channel, size)) {
      GError *error = NULL;
      g_set_error (&error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
          "Requested to send data that is too large");
      _channel_store_error (channel, error);
      _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL,
          NULL);
      return;
    }

    buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, data, size,
        0, size, g_bytes_ref (bytes), (GDestroyNotify) g_bytes_unref);
    ppid = DATA_CHANNEL_PPID_WEBRTC_BINARY;
  }

  _get_sctp_reliability (channel, &reliability, &rel_param);
  gst_sctp_buffer_add_send_meta (buffer, ppid, channel->ordered, reliability,
      rel_param);

  GST_LOG_OBJECT (channel, "Sending data using buffer %" GST_PTR_FORMAT,
      buffer);

  CHANNEL_LOCK (channel);
  channel->buffered_amount += gst_buffer_get_size (buffer);
  CHANNEL_UNLOCK (channel);

  ret = gst_app_src_push_buffer (GST_APP_SRC (channel->appsrc), buffer);

  if (ret != GST_FLOW_OK) {
    GError *error = NULL;
    g_set_error (&error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE, "Failed to send data");
    _channel_store_error (channel, error);
    _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL, NULL);
  }
}

static void
gst_webrtc_data_channel_send_string (GstWebRTCDataChannel * channel,
    gchar * str)
{
  GstSctpSendMetaPartiallyReliability reliability;
  guint rel_param;
  guint32 ppid;
  GstBuffer *buffer;
  GstFlowReturn ret;

  if (!channel->negotiated)
    g_return_if_fail (channel->opened);
  g_return_if_fail (channel->sctp_transport != NULL);

  if (!str) {
    buffer = gst_buffer_new ();
    ppid = DATA_CHANNEL_PPID_WEBRTC_STRING_EMPTY;
  } else {
    gsize size = strlen (str);
    gchar *str_copy = g_strdup (str);

    if (!_is_within_max_message_size (channel, size)) {
      GError *error = NULL;
      g_set_error (&error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE,
          "Requested to send a string that is too large");
      _channel_store_error (channel, error);
      _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL,
          NULL);
      return;
    }

    buffer =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, str_copy,
        size, 0, size, str_copy, g_free);
    ppid = DATA_CHANNEL_PPID_WEBRTC_STRING;
  }

  _get_sctp_reliability (channel, &reliability, &rel_param);
  gst_sctp_buffer_add_send_meta (buffer, ppid, channel->ordered, reliability,
      rel_param);

  GST_TRACE_OBJECT (channel, "Sending string using buffer %" GST_PTR_FORMAT,
      buffer);

  CHANNEL_LOCK (channel);
  channel->buffered_amount += gst_buffer_get_size (buffer);
  CHANNEL_UNLOCK (channel);

  ret = gst_app_src_push_buffer (GST_APP_SRC (channel->appsrc), buffer);

  if (ret != GST_FLOW_OK) {
    GError *error = NULL;
    g_set_error (&error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_DATA_CHANNEL_FAILURE, "Failed to send string");
    _channel_store_error (channel, error);
    _channel_enqueue_task (channel, (ChannelTask) _close_procedure, NULL, NULL);
  }
}

static void
_on_sctp_notify_state_unlocked (GObject * sctp_transport,
    GstWebRTCDataChannel * channel)
{
  GstWebRTCSCTPTransportState state;

  g_object_get (sctp_transport, "state", &state, NULL);
  if (state == GST_WEBRTC_SCTP_TRANSPORT_STATE_CONNECTED) {
    if (channel->negotiated)
      _channel_enqueue_task (channel, (ChannelTask) _emit_on_open, NULL, NULL);
  }
}

static void
_on_sctp_notify_state (GObject * sctp_transport, GParamSpec * pspec,
    GstWebRTCDataChannel * channel)
{
  CHANNEL_LOCK (channel);
  _on_sctp_notify_state_unlocked (sctp_transport, channel);
  CHANNEL_UNLOCK (channel);
}

static void
gst_webrtc_data_channel_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCDataChannel *channel = GST_WEBRTC_DATA_CHANNEL (object);

  CHANNEL_LOCK (channel);
  switch (prop_id) {
    case PROP_LABEL:
      channel->label = g_value_dup_string (value);
      break;
    case PROP_ORDERED:
      channel->ordered = g_value_get_boolean (value);
      break;
    case PROP_MAX_PACKET_LIFETIME:
      channel->max_packet_lifetime = g_value_get_int (value);
      break;
    case PROP_MAX_RETRANSMITS:
      channel->max_retransmits = g_value_get_int (value);
      break;
    case PROP_PROTOCOL:
      channel->protocol = g_value_dup_string (value);
      break;
    case PROP_NEGOTIATED:
      channel->negotiated = g_value_get_boolean (value);
      break;
    case PROP_ID:
      channel->id = g_value_get_int (value);
      break;
    case PROP_PRIORITY:
      channel->priority = g_value_get_enum (value);
      break;
    case PROP_BUFFERED_AMOUNT_LOW_THRESHOLD:
      channel->buffered_amount_low_threshold = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  CHANNEL_UNLOCK (channel);
}

static void
gst_webrtc_data_channel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCDataChannel *channel = GST_WEBRTC_DATA_CHANNEL (object);

  CHANNEL_LOCK (channel);
  switch (prop_id) {
    case PROP_LABEL:
      g_value_set_string (value, channel->label);
      break;
    case PROP_ORDERED:
      g_value_set_boolean (value, channel->ordered);
      break;
    case PROP_MAX_PACKET_LIFETIME:
      g_value_set_int (value, channel->max_packet_lifetime);
      break;
    case PROP_MAX_RETRANSMITS:
      g_value_set_int (value, channel->max_retransmits);
      break;
    case PROP_PROTOCOL:
      g_value_set_string (value, channel->protocol);
      break;
    case PROP_NEGOTIATED:
      g_value_set_boolean (value, channel->negotiated);
      break;
    case PROP_ID:
      g_value_set_int (value, channel->id);
      break;
    case PROP_PRIORITY:
      g_value_set_enum (value, channel->priority);
      break;
    case PROP_READY_STATE:
      g_value_set_enum (value, channel->ready_state);
      break;
    case PROP_BUFFERED_AMOUNT:
      g_value_set_uint64 (value, channel->buffered_amount);
      break;
    case PROP_BUFFERED_AMOUNT_LOW_THRESHOLD:
      g_value_set_uint64 (value, channel->buffered_amount_low_threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  CHANNEL_UNLOCK (channel);
}

static void
_emit_low_threshold (GstWebRTCDataChannel * channel, gpointer user_data)
{
  GST_LOG_OBJECT (channel, "Low threshold reached");
  g_signal_emit (channel,
      gst_webrtc_data_channel_signals[SIGNAL_ON_BUFFERED_AMOUNT_LOW], 0);
}

static GstPadProbeReturn
on_appsrc_data (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstWebRTCDataChannel *channel = user_data;
  guint64 prev_amount;
  guint64 size = 0;

  if (GST_PAD_PROBE_INFO_TYPE (info) & (GST_PAD_PROBE_TYPE_BUFFER)) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    size = gst_buffer_get_size (buffer);
  } else if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    GstBufferList *list = GST_PAD_PROBE_INFO_BUFFER_LIST (info);
    size = gst_buffer_list_calculate_size (list);
  }

  if (size > 0) {
    CHANNEL_LOCK (channel);
    prev_amount = channel->buffered_amount;
    channel->buffered_amount -= size;
    if (prev_amount > channel->buffered_amount_low_threshold &&
        channel->buffered_amount < channel->buffered_amount_low_threshold) {
      _channel_enqueue_task (channel, (ChannelTask) _emit_low_threshold,
          NULL, NULL);
    }

    if (channel->ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_CLOSING
        && channel->buffered_amount <= 0) {
      _channel_enqueue_task (channel, (ChannelTask) _close_sctp_stream, NULL,
          NULL);
    }
    CHANNEL_UNLOCK (channel);
  }

  return GST_PAD_PROBE_OK;
}

static void
gst_webrtc_data_channel_constructed (GObject * object)
{
  GstWebRTCDataChannel *channel = GST_WEBRTC_DATA_CHANNEL (object);
  GstPad *pad;
  GstCaps *caps;

  caps = gst_caps_new_any ();

  channel->appsrc = gst_element_factory_make ("appsrc", NULL);
  gst_object_ref_sink (channel->appsrc);
  pad = gst_element_get_static_pad (channel->appsrc, "src");

  channel->src_probe = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_DATA_BOTH,
      (GstPadProbeCallback) on_appsrc_data, channel, NULL);

  channel->appsink = gst_element_factory_make ("appsink", NULL);
  gst_object_ref_sink (channel->appsink);
  g_object_set (channel->appsink, "sync", FALSE, "async", FALSE, "caps", caps,
      NULL);
  gst_app_sink_set_callbacks (GST_APP_SINK (channel->appsink), &sink_callbacks,
      channel, NULL);

  gst_object_unref (pad);
  gst_caps_unref (caps);
}

static void
gst_webrtc_data_channel_finalize (GObject * object)
{
  GstWebRTCDataChannel *channel = GST_WEBRTC_DATA_CHANNEL (object);

  if (channel->src_probe) {
    GstPad *pad = gst_element_get_static_pad (channel->appsrc, "src");
    gst_pad_remove_probe (pad, channel->src_probe);
    gst_object_unref (pad);
    channel->src_probe = 0;
  }

  g_free (channel->label);
  channel->label = NULL;

  g_free (channel->protocol);
  channel->protocol = NULL;

  if (channel->sctp_transport)
    g_signal_handlers_disconnect_by_data (channel->sctp_transport, channel);
  g_clear_object (&channel->sctp_transport);

  g_clear_object (&channel->appsrc);
  g_clear_object (&channel->appsink);

  g_mutex_clear (&channel->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_data_channel_class_init (GstWebRTCDataChannelClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->constructed = gst_webrtc_data_channel_constructed;
  gobject_class->get_property = gst_webrtc_data_channel_get_property;
  gobject_class->set_property = gst_webrtc_data_channel_set_property;
  gobject_class->finalize = gst_webrtc_data_channel_finalize;

  g_object_class_install_property (gobject_class,
      PROP_LABEL,
      g_param_spec_string ("label",
          "Label", "Data channel label",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ORDERED,
      g_param_spec_boolean ("ordered",
          "Ordered", "Using ordered transmission mode",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MAX_PACKET_LIFETIME,
      g_param_spec_int ("max-packet-lifetime",
          "Maximum Packet Lifetime",
          "Maximum number of milliseconds that transmissions and "
          "retransmissions may occur in unreliable mode (-1 = unset)",
          -1, G_MAXUINT16, -1,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MAX_RETRANSMITS,
      g_param_spec_int ("max-retransmits",
          "Maximum Retransmits",
          "Maximum number of retransmissions attempted in unreliable mode",
          -1, G_MAXUINT16, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_PROTOCOL,
      g_param_spec_string ("protocol",
          "Protocol", "Data channel protocol",
          "",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_NEGOTIATED,
      g_param_spec_boolean ("negotiated",
          "Negotiated",
          "Whether this data channel was negotiated by the application", FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ID,
      g_param_spec_int ("id",
          "ID",
          "ID negotiated by this data channel (-1 = unset)",
          -1, G_MAXUINT16, -1,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_PRIORITY,
      g_param_spec_enum ("priority",
          "Priority",
          "The priority of data sent using this data channel",
          GST_TYPE_WEBRTC_PRIORITY_TYPE,
          GST_WEBRTC_PRIORITY_TYPE_LOW,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_READY_STATE,
      g_param_spec_enum ("ready-state",
          "Ready State",
          "The Ready state of this data channel",
          GST_TYPE_WEBRTC_DATA_CHANNEL_STATE,
          GST_WEBRTC_DATA_CHANNEL_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUFFERED_AMOUNT,
      g_param_spec_uint64 ("buffered-amount",
          "Buffered Amount",
          "The amount of data in bytes currently buffered",
          0, G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUFFERED_AMOUNT_LOW_THRESHOLD,
      g_param_spec_uint64 ("buffered-amount-low-threshold",
          "Buffered Amount Low Threshold",
          "The threshold at which the buffered amount is considered low and "
          "the buffered-amount-low signal is emitted",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstWebRTCDataChannel::on-open:
   * @object: the #GstWebRTCDataChannel
   */
  gst_webrtc_data_channel_signals[SIGNAL_ON_OPEN] =
      g_signal_new ("on-open", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 0);

  /**
   * GstWebRTCDataChannel::on-close:
   * @object: the #GstWebRTCDataChannel
   */
  gst_webrtc_data_channel_signals[SIGNAL_ON_CLOSE] =
      g_signal_new ("on-close", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 0);

  /**
   * GstWebRTCDataChannel::on-error:
   * @object: the #GstWebRTCDataChannel
   * @error: the #GError thrown
   */
  gst_webrtc_data_channel_signals[SIGNAL_ON_ERROR] =
      g_signal_new ("on-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_ERROR);

  /**
   * GstWebRTCDataChannel::on-message-data:
   * @object: the #GstWebRTCDataChannel
   * @data: (nullable): a #GBytes of the data received
   */
  gst_webrtc_data_channel_signals[SIGNAL_ON_MESSAGE_DATA] =
      g_signal_new ("on-message-data", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_BYTES);

  /**
   * GstWebRTCDataChannel::on-message-string:
   * @object: the #GstWebRTCDataChannel
   * @data: (nullable): the data received as a string
   */
  gst_webrtc_data_channel_signals[SIGNAL_ON_MESSAGE_STRING] =
      g_signal_new ("on-message-string", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * GstWebRTCDataChannel::on-buffered-amount-low:
   * @object: the #GstWebRTCDataChannel
   */
  gst_webrtc_data_channel_signals[SIGNAL_ON_BUFFERED_AMOUNT_LOW] =
      g_signal_new ("on-buffered-amount-low", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 0);

  /**
   * GstWebRTCDataChannel::send-data:
   * @object: the #GstWebRTCDataChannel
   * @data: (nullable): a #GBytes with the data
   */
  gst_webrtc_data_channel_signals[SIGNAL_SEND_DATA] =
      g_signal_new_class_handler ("send-data", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_data_channel_send_data), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_BYTES);

  /**
   * GstWebRTCDataChannel::send-string:
   * @object: the #GstWebRTCDataChannel
   * @data: (nullable): the data to send as a string
   */
  gst_webrtc_data_channel_signals[SIGNAL_SEND_STRING] =
      g_signal_new_class_handler ("send-string", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_data_channel_send_string), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * GstWebRTCDataChannel::close:
   * @object: the #GstWebRTCDataChannel
   *
   * Close the data channel
   */
  gst_webrtc_data_channel_signals[SIGNAL_CLOSE] =
      g_signal_new_class_handler ("close", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_data_channel_close), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0);
}

static void
gst_webrtc_data_channel_init (GstWebRTCDataChannel * channel)
{
  g_mutex_init (&channel->lock);
}

static void
_data_channel_set_sctp_transport (GstWebRTCDataChannel * channel,
    GstWebRTCSCTPTransport * sctp)
{
  g_return_if_fail (GST_IS_WEBRTC_DATA_CHANNEL (channel));
  g_return_if_fail (GST_IS_WEBRTC_SCTP_TRANSPORT (sctp));

  CHANNEL_LOCK (channel);
  if (channel->sctp_transport)
    g_signal_handlers_disconnect_by_data (channel->sctp_transport, channel);

  gst_object_replace ((GstObject **) & channel->sctp_transport,
      GST_OBJECT (sctp));

  if (sctp) {
    g_signal_connect (sctp, "stream-reset", G_CALLBACK (_on_sctp_reset_stream),
        channel);
    g_signal_connect (sctp, "notify::state", G_CALLBACK (_on_sctp_notify_state),
        channel);
    _on_sctp_notify_state_unlocked (G_OBJECT (sctp), channel);
  }
  CHANNEL_UNLOCK (channel);
}

void
gst_webrtc_data_channel_link_to_sctp (GstWebRTCDataChannel * channel,
    GstWebRTCSCTPTransport * sctp_transport)
{
  if (sctp_transport && !channel->sctp_transport) {
    gint id;

    g_object_get (channel, "id", &id, NULL);

    if (sctp_transport->association_established && id != -1) {
      gchar *pad_name;

      _data_channel_set_sctp_transport (channel, sctp_transport);
      pad_name = g_strdup_printf ("sink_%u", id);
      if (!gst_element_link_pads (channel->appsrc, "src",
              channel->sctp_transport->sctpenc, pad_name))
        g_warn_if_reached ();
      g_free (pad_name);
    }
  }
}
