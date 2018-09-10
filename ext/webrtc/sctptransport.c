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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#include "sctptransport.h"
#include "gstwebrtcbin.h"

#define GST_CAT_DEFAULT gst_webrtc_sctp_transport_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  SIGNAL_0,
  ON_RESET_STREAM_SIGNAL,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_TRANSPORT,
  PROP_STATE,
  PROP_MAX_MESSAGE_SIZE,
  PROP_MAX_CHANNELS,
};

static guint gst_webrtc_sctp_transport_signals[LAST_SIGNAL] = { 0 };

#define gst_webrtc_sctp_transport_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCSCTPTransport, gst_webrtc_sctp_transport,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (gst_webrtc_sctp_transport_debug,
        "webrtcsctptransport", 0, "webrtcsctptransport"););

typedef void (*SCTPTask) (GstWebRTCSCTPTransport * sctp, gpointer user_data);

struct task
{
  GstWebRTCSCTPTransport *sctp;
  SCTPTask func;
  gpointer user_data;
  GDestroyNotify notify;
};

static void
_execute_task (GstWebRTCBin * webrtc, struct task *task)
{
  if (task->func)
    task->func (task->sctp, task->user_data);
}

static void
_free_task (struct task *task)
{
  gst_object_unref (task->sctp);

  if (task->notify)
    task->notify (task->user_data);
  g_free (task);
}

static void
_sctp_enqueue_task (GstWebRTCSCTPTransport * sctp, SCTPTask func,
    gpointer user_data, GDestroyNotify notify)
{
  struct task *task = g_new0 (struct task, 1);

  task->sctp = gst_object_ref (sctp);
  task->func = func;
  task->user_data = user_data;
  task->notify = notify;

  gst_webrtc_bin_enqueue_task (sctp->webrtcbin,
      (GstWebRTCBinFunc) _execute_task, task, (GDestroyNotify) _free_task);
}

static void
_emit_stream_reset (GstWebRTCSCTPTransport * sctp, gpointer user_data)
{
  guint stream_id = GPOINTER_TO_UINT (user_data);

  g_signal_emit (sctp,
      gst_webrtc_sctp_transport_signals[ON_RESET_STREAM_SIGNAL], 0, stream_id);
}

static void
_on_sctp_dec_pad_removed (GstElement * sctpdec, GstPad * pad,
    GstWebRTCSCTPTransport * sctp)
{
  guint stream_id;

  if (sscanf (GST_PAD_NAME (pad), "src_%u", &stream_id) != 1)
    return;

  _sctp_enqueue_task (sctp, (SCTPTask) _emit_stream_reset,
      GUINT_TO_POINTER (stream_id), NULL);
}

static void
_on_sctp_association_established (GstElement * sctpenc, gboolean established,
    GstWebRTCSCTPTransport * sctp)
{
  GST_OBJECT_LOCK (sctp);
  if (established)
    sctp->state = GST_WEBRTC_SCTP_TRANSPORT_STATE_CONNECTED;
  else
    sctp->state = GST_WEBRTC_SCTP_TRANSPORT_STATE_CLOSED;
  sctp->association_established = established;
  GST_OBJECT_UNLOCK (sctp);

  g_object_notify (G_OBJECT (sctp), "state");
}

static void
gst_webrtc_sctp_transport_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
//  GstWebRTCSCTPTransport *sctp = GST_WEBRTC_SCTP_TRANSPORT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_sctp_transport_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCSCTPTransport *sctp = GST_WEBRTC_SCTP_TRANSPORT (object);

  switch (prop_id) {
    case PROP_TRANSPORT:
      g_value_set_object (value, sctp->transport);
      break;
    case PROP_STATE:
      g_value_set_enum (value, sctp->state);
      break;
    case PROP_MAX_MESSAGE_SIZE:
      g_value_set_uint64 (value, sctp->max_message_size);
      break;
    case PROP_MAX_CHANNELS:
      g_value_set_uint (value, sctp->max_channels);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_sctp_transport_finalize (GObject * object)
{
  GstWebRTCSCTPTransport *sctp = GST_WEBRTC_SCTP_TRANSPORT (object);

  g_signal_handlers_disconnect_by_data (sctp->sctpdec, sctp);
  g_signal_handlers_disconnect_by_data (sctp->sctpenc, sctp);

  gst_object_unref (sctp->sctpdec);
  gst_object_unref (sctp->sctpenc);

  g_clear_object (&sctp->transport);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_sctp_transport_constructed (GObject * object)
{
  GstWebRTCSCTPTransport *sctp = GST_WEBRTC_SCTP_TRANSPORT (object);
  guint association_id;

  association_id = g_random_int_range (0, G_MAXUINT16);

  sctp->sctpdec =
      g_object_ref_sink (gst_element_factory_make ("sctpdec", NULL));
  g_object_set (sctp->sctpdec, "sctp-association-id", association_id, NULL);
  sctp->sctpenc =
      g_object_ref_sink (gst_element_factory_make ("sctpenc", NULL));
  g_object_set (sctp->sctpenc, "sctp-association-id", association_id, NULL);

  g_signal_connect (sctp->sctpdec, "pad-removed",
      G_CALLBACK (_on_sctp_dec_pad_removed), sctp);
  g_signal_connect (sctp->sctpenc, "sctp-association-established",
      G_CALLBACK (_on_sctp_association_established), sctp);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_webrtc_sctp_transport_class_init (GstWebRTCSCTPTransportClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->constructed = gst_webrtc_sctp_transport_constructed;
  gobject_class->get_property = gst_webrtc_sctp_transport_get_property;
  gobject_class->set_property = gst_webrtc_sctp_transport_set_property;
  gobject_class->finalize = gst_webrtc_sctp_transport_finalize;

  g_object_class_install_property (gobject_class,
      PROP_TRANSPORT,
      g_param_spec_object ("transport",
          "WebRTC DTLS Transport",
          "DTLS transport used for this SCTP transport",
          GST_TYPE_WEBRTC_DTLS_TRANSPORT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_STATE,
      g_param_spec_enum ("state",
          "WebRTC SCTP Transport state", "WebRTC SCTP Transport state",
          GST_TYPE_WEBRTC_SCTP_TRANSPORT_STATE,
          GST_WEBRTC_SCTP_TRANSPORT_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MAX_MESSAGE_SIZE,
      g_param_spec_uint64 ("max-message-size",
          "Maximum message size",
          "Maximum message size as reported by the transport", 0, G_MAXUINT64,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MAX_CHANNELS,
      g_param_spec_uint ("max-channels",
          "Maximum number of channels", "Maximum number of channels",
          0, G_MAXUINT16, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstWebRTCSCTPTransport::reset-stream:
   * @object: the #GstWebRTCSCTPTransport
   * @stream_id: the SCTP stream that was reset
   */
  gst_webrtc_sctp_transport_signals[ON_RESET_STREAM_SIGNAL] =
      g_signal_new ("stream-reset", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
gst_webrtc_sctp_transport_init (GstWebRTCSCTPTransport * nice)
{
}

GstWebRTCSCTPTransport *
gst_webrtc_sctp_transport_new (void)
{
  return g_object_new (GST_TYPE_WEBRTC_SCTP_TRANSPORT, NULL);
}
