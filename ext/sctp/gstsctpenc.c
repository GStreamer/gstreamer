/*
 * Copyright (c) 2015, Collabora Ltd.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstsctpenc.h"

#include <gst/sctp/sctpsendmeta.h>
#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_sctp_enc_debug_category);
#define GST_CAT_DEFAULT gst_sctp_enc_debug_category

#define gst_sctp_enc_parent_class parent_class
G_DEFINE_TYPE (GstSctpEnc, gst_sctp_enc, GST_TYPE_ELEMENT);

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK,
    GST_PAD_REQUEST, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS, GST_STATIC_CAPS ("application/x-sctp"));

enum
{
  SIGNAL_SCTP_ASSOCIATION_ESTABLISHED,
  SIGNAL_GET_STREAM_BYTES_SENT,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

enum
{
  PROP_0,

  PROP_GST_SCTP_ASSOCIATION_ID,
  PROP_REMOTE_SCTP_PORT,
  PROP_USE_SOCK_STREAM,

  NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

#define DEFAULT_GST_SCTP_ASSOCIATION_ID 1
#define DEFAULT_REMOTE_SCTP_PORT 0
#define DEFAULT_GST_SCTP_ORDERED TRUE
#define DEFAULT_SCTP_PPID 1
#define DEFAULT_USE_SOCK_STREAM FALSE

#define BUFFER_FULL_SLEEP_TIME 100000

GType gst_sctp_enc_pad_get_type (void);

#define GST_TYPE_SCTP_ENC_PAD (gst_sctp_enc_pad_get_type())
#define GST_SCTP_ENC_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SCTP_ENC_PAD, GstSctpEncPad))
#define GST_SCTP_ENC_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SCTP_ENC_PAD, GstSctpEncPadClass))
#define GST_IS_SCTP_ENC_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SCTP_ENC_PAD))
#define GST_IS_SCTP_ENC_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SCTP_ENC_PAD))

typedef struct _GstSctpEncPad GstSctpEncPad;
typedef GstPadClass GstSctpEncPadClass;

struct _GstSctpEncPad
{
  GstPad parent;

  guint16 stream_id;
  gboolean ordered;
  guint32 ppid;
  GstSctpAssociationPartialReliability reliability;
  guint32 reliability_param;

  guint64 bytes_sent;

  GMutex lock;
  GCond cond;
  gboolean flushing;
};

G_DEFINE_TYPE (GstSctpEncPad, gst_sctp_enc_pad, GST_TYPE_PAD);

static void
gst_sctp_enc_pad_finalize (GObject * object)
{
  GstSctpEncPad *self = GST_SCTP_ENC_PAD (object);

  g_cond_clear (&self->cond);
  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (gst_sctp_enc_pad_parent_class)->finalize (object);
}

static void
gst_sctp_enc_pad_class_init (GstSctpEncPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_sctp_enc_pad_finalize;
}

static void
gst_sctp_enc_pad_init (GstSctpEncPad * self)
{
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);
  self->flushing = FALSE;
}

static void gst_sctp_enc_finalize (GObject * object);
static void gst_sctp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sctp_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_sctp_enc_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_sctp_enc_request_new_pad (GstElement * element,
    GstPadTemplate * template, const gchar * name, const GstCaps * caps);
static void gst_sctp_enc_release_pad (GstElement * element, GstPad * pad);
static void gst_sctp_enc_srcpad_loop (GstPad * pad);
static GstFlowReturn gst_sctp_enc_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_sctp_enc_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_sctp_enc_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static void on_sctp_association_state_changed (GstSctpAssociation *
    sctp_association, GParamSpec * pspec, GstSctpEnc * self);

static gboolean configure_association (GstSctpEnc * self);
static void on_sctp_packet_out (GstSctpAssociation * sctp_association,
    const guint8 * buf, gsize length, gpointer user_data);
static void stop_srcpad_task (GstPad * pad, GstSctpEnc * self);
static void sctpenc_cleanup (GstSctpEnc * self);
static void get_config_from_caps (const GstCaps * caps, gboolean * ordered,
    GstSctpAssociationPartialReliability * reliability,
    guint32 * reliability_param, guint32 * ppid, gboolean * ppid_available);
static guint64 on_get_stream_bytes_sent (GstSctpEnc * self, guint stream_id);

static void
gst_sctp_enc_class_init (GstSctpEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_sctp_enc_debug_category,
      "sctpenc", 0, "debug category for sctpenc element");

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&sink_template));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_sctp_enc_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_sctp_enc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_sctp_enc_get_property);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_sctp_enc_change_state);
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_sctp_enc_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_sctp_enc_release_pad);

  properties[PROP_GST_SCTP_ASSOCIATION_ID] =
      g_param_spec_uint ("sctp-association-id",
      "SCTP Association ID",
      "Every encoder/decoder pair should have the same, unique, sctp-association-id. "
      "This value must be set before any pads are requested.",
      0, G_MAXUINT, DEFAULT_GST_SCTP_ASSOCIATION_ID,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_REMOTE_SCTP_PORT] =
      g_param_spec_uint ("remote-sctp-port",
      "Remote SCTP port",
      "Sctp remote sctp port for the sctp association. The local port is configured via the "
      "GstSctpDec element.",
      0, G_MAXUSHORT, DEFAULT_REMOTE_SCTP_PORT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_USE_SOCK_STREAM] =
      g_param_spec_boolean ("use-sock-stream",
      "Use sock-stream",
      "When set to TRUE, a sequenced, reliable, connection-based connection is used."
      "When TRUE the partial reliability parameters of the channel are ignored.",
      DEFAULT_USE_SOCK_STREAM, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);

  signals[SIGNAL_SCTP_ASSOCIATION_ESTABLISHED] =
      g_signal_new ("sctp-association-established",
      G_TYPE_FROM_CLASS (gobject_class), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstSctpEncClass, on_sctp_association_is_established),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  signals[SIGNAL_GET_STREAM_BYTES_SENT] = g_signal_new ("bytes-sent",
      G_TYPE_FROM_CLASS (gobject_class), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstSctpEncClass, on_get_stream_bytes_sent), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_UINT64, 1, G_TYPE_UINT);

  klass->on_get_stream_bytes_sent =
      GST_DEBUG_FUNCPTR (on_get_stream_bytes_sent);

  gst_element_class_set_static_metadata (element_class,
      "SCTP Encoder",
      "Encoder/Network/SCTP",
      "Encodes packets with SCTP",
      "George Kiagiadakis <george.kiagiadakis@collabora.com>");
}

static gboolean
data_queue_check_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer user_data)
{
  /* TODO: When are we considered full? */
  return FALSE;
}

static void
data_queue_empty_cb (GstDataQueue * queue, gpointer user_data)
{
}

static void
data_queue_full_cb (GstDataQueue * queue, gpointer user_data)
{
}

static void
gst_sctp_enc_init (GstSctpEnc * self)
{
  self->sctp_association_id = DEFAULT_GST_SCTP_ASSOCIATION_ID;
  self->remote_sctp_port = DEFAULT_REMOTE_SCTP_PORT;

  self->sctp_association = NULL;
  self->outbound_sctp_packet_queue =
      gst_data_queue_new (data_queue_check_full_cb, data_queue_full_cb,
      data_queue_empty_cb, NULL);

  self->src_pad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (self->src_pad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) gst_sctp_enc_src_event));
  gst_element_add_pad (GST_ELEMENT (self), self->src_pad);

  g_queue_init (&self->pending_pads);
}

static void
gst_sctp_enc_finalize (GObject * object)
{
  GstSctpEnc *self = GST_SCTP_ENC (object);

  g_queue_clear (&self->pending_pads);
  gst_object_unref (self->outbound_sctp_packet_queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_sctp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSctpEnc *self = GST_SCTP_ENC (object);

  switch (prop_id) {
    case PROP_GST_SCTP_ASSOCIATION_ID:
      self->sctp_association_id = g_value_get_uint (value);
      break;
    case PROP_REMOTE_SCTP_PORT:
      self->remote_sctp_port = g_value_get_uint (value);
      break;
    case PROP_USE_SOCK_STREAM:
      self->use_sock_stream = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_sctp_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSctpEnc *self = GST_SCTP_ENC (object);

  switch (prop_id) {
    case PROP_GST_SCTP_ASSOCIATION_ID:
      g_value_set_uint (value, self->sctp_association_id);
      break;
    case PROP_REMOTE_SCTP_PORT:
      g_value_set_uint (value, self->remote_sctp_port);
      break;
    case PROP_USE_SOCK_STREAM:
      g_value_set_boolean (value, self->use_sock_stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_sctp_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstSctpEnc *self = GST_SCTP_ENC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  gboolean res = TRUE;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->need_segment = self->need_stream_start_caps = TRUE;
      gst_data_queue_set_flushing (self->outbound_sctp_packet_queue, FALSE);
      res = configure_association (self);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      sctpenc_cleanup (self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (res)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_pad_start_task (self->src_pad,
          (GstTaskFunction) gst_sctp_enc_srcpad_loop, self->src_pad, NULL);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstPad *
gst_sctp_enc_request_new_pad (GstElement * element, GstPadTemplate * template,
    const gchar * new_pad_name, const GstCaps * caps)
{
  GstSctpEnc *self = GST_SCTP_ENC (element);
  GstPad *new_pad = NULL;
  GstSctpEncPad *sctpenc_pad;
  guint32 stream_id;
  gint state;
  guint32 new_ppid;
  gboolean is_new_ppid;

  g_object_get (self->sctp_association, "state", &state, NULL);

  if (state != GST_SCTP_ASSOCIATION_STATE_CONNECTED) {
    g_warning
        ("The SCTP association must be established before a new stream can be created");
    goto invalid_state;
  }

  if (!template)
    goto invalid_parameter;

  if (!new_pad_name || (sscanf (new_pad_name, "sink_%u", &stream_id) != 1)
      || stream_id > 65534)     /* 65535 is not a valid stream id */
    goto invalid_parameter;

  new_pad = gst_element_get_static_pad (element, new_pad_name);
  if (new_pad) {
    gst_object_unref (new_pad);
    new_pad = NULL;
    goto invalid_parameter;
  }

  new_pad =
      g_object_new (GST_TYPE_SCTP_ENC_PAD, "name", new_pad_name, "direction",
      template->direction, "template", template, NULL);
  gst_pad_set_chain_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_sctp_enc_sink_chain));
  gst_pad_set_event_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_sctp_enc_sink_event));

  sctpenc_pad = GST_SCTP_ENC_PAD (new_pad);
  sctpenc_pad->stream_id = stream_id;
  sctpenc_pad->ppid = DEFAULT_SCTP_PPID;

  if (caps) {
    get_config_from_caps (caps, &sctpenc_pad->ordered,
        &sctpenc_pad->reliability, &sctpenc_pad->reliability_param, &new_ppid,
        &is_new_ppid);

    if (is_new_ppid)
      sctpenc_pad->ppid = new_ppid;
  }

  sctpenc_pad->flushing = FALSE;

  if (!gst_pad_set_active (new_pad, TRUE))
    goto error_cleanup;

  if (!gst_element_add_pad (element, new_pad))
    goto error_cleanup;

invalid_state:
invalid_parameter:
  return new_pad;
error_cleanup:
  gst_object_unref (new_pad);
  return NULL;
}

static void
gst_sctp_enc_release_pad (GstElement * element, GstPad * pad)
{
  GstSctpEncPad *sctpenc_pad = GST_SCTP_ENC_PAD (pad);
  GstSctpEnc *self;
  guint stream_id = 0;

  self = GST_SCTP_ENC (element);

  g_mutex_lock (&sctpenc_pad->lock);
  sctpenc_pad->flushing = TRUE;
  g_cond_signal (&sctpenc_pad->cond);
  g_mutex_unlock (&sctpenc_pad->lock);

  stream_id = sctpenc_pad->stream_id;
  gst_pad_set_active (pad, FALSE);

  if (self->sctp_association)
    gst_sctp_association_reset_stream (self->sctp_association, stream_id);

  gst_element_remove_pad (element, pad);
}

static void
gst_sctp_enc_srcpad_loop (GstPad * pad)
{
  GstSctpEnc *self = GST_SCTP_ENC (GST_PAD_PARENT (pad));
  GstFlowReturn flow_ret;
  GstDataQueueItem *item;

  if (self->need_stream_start_caps) {
    gchar s_id[32];
    GstCaps *caps;

    g_snprintf (s_id, sizeof (s_id), "sctpenc-%08x", g_random_int ());
    gst_pad_push_event (self->src_pad, gst_event_new_stream_start (s_id));

    caps = gst_caps_new_empty_simple ("application/x-sctp");
    gst_pad_set_caps (self->src_pad, caps);
    gst_caps_unref (caps);

    self->need_stream_start_caps = FALSE;
  }

  if (self->need_segment) {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    gst_pad_push_event (self->src_pad, gst_event_new_segment (&segment));

    self->need_segment = FALSE;
  }

  if (gst_data_queue_pop (self->outbound_sctp_packet_queue, &item)) {
    flow_ret = gst_pad_push (self->src_pad, GST_BUFFER (item->object));
    item->object = NULL;

    if (G_UNLIKELY (flow_ret == GST_FLOW_FLUSHING
            || flow_ret == GST_FLOW_NOT_LINKED)) {
      GST_DEBUG_OBJECT (pad, "Push failed on packet source pad. Error: %s",
          gst_flow_get_name (flow_ret));
    } else if (G_UNLIKELY (flow_ret != GST_FLOW_OK)) {
      GST_ERROR_OBJECT (pad, "Push failed on packet source pad. Error: %s",
          gst_flow_get_name (flow_ret));
    }

    if (G_UNLIKELY (flow_ret != GST_FLOW_OK)) {
      GST_DEBUG_OBJECT (pad, "Pausing task because of an error");
      gst_data_queue_set_flushing (self->outbound_sctp_packet_queue, TRUE);
      gst_data_queue_flush (self->outbound_sctp_packet_queue);
      gst_pad_pause_task (pad);
    }

    item->destroy (item);
  } else {
    GST_DEBUG_OBJECT (pad, "Pausing task because we're flushing");
    gst_pad_pause_task (pad);
  }
}

static GstFlowReturn
gst_sctp_enc_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSctpEnc *self = GST_SCTP_ENC (parent);
  GstSctpEncPad *sctpenc_pad = GST_SCTP_ENC_PAD (pad);
  GstMapInfo map;
  guint32 ppid;
  gboolean ordered;
  GstSctpAssociationPartialReliability pr;
  guint32 pr_param;
  gpointer state = NULL;
  GstMeta *meta;
  const GstMetaInfo *meta_info = GST_SCTP_SEND_META_INFO;
  GstFlowReturn flow_ret = GST_FLOW_ERROR;

  ppid = sctpenc_pad->ppid;
  ordered = sctpenc_pad->ordered;
  pr = sctpenc_pad->reliability;
  pr_param = sctpenc_pad->reliability_param;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == meta_info->api) {
      GstSctpSendMeta *sctp_send_meta = (GstSctpSendMeta *) meta;

      ppid = sctp_send_meta->ppid;
      ordered = sctp_send_meta->ordered;
      pr_param = sctp_send_meta->pr_param;
      switch (sctp_send_meta->pr) {
        case GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE:
          pr = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_NONE;
          break;
        case GST_SCTP_SEND_META_PARTIAL_RELIABILITY_RTX:
          pr = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_RTX;
          break;
        case GST_SCTP_SEND_META_PARTIAL_RELIABILITY_BUF:
          pr = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_BUF;
          break;
        case GST_SCTP_SEND_META_PARTIAL_RELIABILITY_TTL:
          pr = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_TTL;
          break;
      }
      break;
    }
  }

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    g_warning ("Could not map GstBuffer");
    goto error;
  }

  g_mutex_lock (&sctpenc_pad->lock);
  while (!sctpenc_pad->flushing) {
    gboolean data_sent = FALSE;

    g_mutex_unlock (&sctpenc_pad->lock);

    data_sent =
        gst_sctp_association_send_data (self->sctp_association, map.data,
        map.size, sctpenc_pad->stream_id, ppid, ordered, pr, pr_param);

    g_mutex_lock (&sctpenc_pad->lock);
    if (data_sent) {
      sctpenc_pad->bytes_sent += map.size;
      break;
    } else if (!sctpenc_pad->flushing) {
      gint64 end_time = g_get_monotonic_time () + BUFFER_FULL_SLEEP_TIME;

      /* The buffer was probably full. Retry in a while */
      GST_OBJECT_LOCK (self);
      g_queue_push_tail (&self->pending_pads, sctpenc_pad);
      GST_OBJECT_UNLOCK (self);

      g_cond_wait_until (&sctpenc_pad->cond, &sctpenc_pad->lock, end_time);

      GST_OBJECT_LOCK (self);
      g_queue_remove (&self->pending_pads, sctpenc_pad);
      GST_OBJECT_UNLOCK (self);
    }
  }
  flow_ret = sctpenc_pad->flushing ? GST_FLOW_FLUSHING : GST_FLOW_OK;
  g_mutex_unlock (&sctpenc_pad->lock);

  gst_buffer_unmap (buffer, &map);
error:
  gst_buffer_unref (buffer);
  return flow_ret;
}

static gboolean
gst_sctp_enc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSctpEncPad *sctpenc_pad = GST_SCTP_ENC_PAD (pad);
  gboolean ret, is_new_ppid;
  guint32 new_ppid;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      get_config_from_caps (caps, &sctpenc_pad->ordered,
          &sctpenc_pad->reliability, &sctpenc_pad->reliability_param, &new_ppid,
          &is_new_ppid);
      if (is_new_ppid)
        sctpenc_pad->ppid = new_ppid;
      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_STREAM_START:
    case GST_EVENT_SEGMENT:
      /* Drop these, we create our own */
      ret = TRUE;
      gst_event_unref (event);
      break;
    case GST_EVENT_EOS:
      /* Drop this, we're never EOS until shut down */
      ret = TRUE;
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_START:
      g_mutex_lock (&sctpenc_pad->lock);
      sctpenc_pad->flushing = TRUE;
      g_cond_signal (&sctpenc_pad->cond);
      g_mutex_unlock (&sctpenc_pad->lock);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      sctpenc_pad->flushing = FALSE;
      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static void
flush_sinkpad (const GValue * item, gpointer user_data)
{
  GstSctpEncPad *sctpenc_pad = g_value_get_object (item);
  gboolean flush = GPOINTER_TO_INT (user_data);

  if (flush) {
    g_mutex_lock (&sctpenc_pad->lock);
    sctpenc_pad->flushing = TRUE;
    g_cond_signal (&sctpenc_pad->cond);
    g_mutex_unlock (&sctpenc_pad->lock);
  } else {
    sctpenc_pad->flushing = FALSE;
  }
}

static gboolean
gst_sctp_enc_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSctpEnc *self = GST_SCTP_ENC (parent);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:{
      GstIterator *it;

      gst_data_queue_set_flushing (self->outbound_sctp_packet_queue, TRUE);
      gst_data_queue_flush (self->outbound_sctp_packet_queue);

      it = gst_element_iterate_sink_pads (GST_ELEMENT (self));
      while (gst_iterator_foreach (it, flush_sinkpad,
              GINT_TO_POINTER (TRUE)) == GST_ITERATOR_RESYNC)
        gst_iterator_resync (it);
      gst_iterator_free (it);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_RECONFIGURE:
    case GST_EVENT_FLUSH_STOP:{
      GstIterator *it;

      it = gst_element_iterate_sink_pads (GST_ELEMENT (self));
      while (gst_iterator_foreach (it, flush_sinkpad,
              GINT_TO_POINTER (FALSE)) == GST_ITERATOR_RESYNC)
        gst_iterator_resync (it);
      gst_iterator_free (it);

      gst_data_queue_set_flushing (self->outbound_sctp_packet_queue, FALSE);
      self->need_segment = TRUE;
      gst_pad_start_task (self->src_pad,
          (GstTaskFunction) gst_sctp_enc_srcpad_loop, self->src_pad, NULL);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static gboolean
configure_association (GstSctpEnc * self)
{
  gint state;

  self->sctp_association = gst_sctp_association_get (self->sctp_association_id);

  g_object_get (self->sctp_association, "state", &state, NULL);

  if (state != GST_SCTP_ASSOCIATION_STATE_NEW) {
    GST_WARNING_OBJECT (self,
        "Could not configure SCTP association. Association already in use!");
    g_object_unref (self->sctp_association);
    self->sctp_association = NULL;
    goto error;
  }

  self->signal_handler_state_changed =
      g_signal_connect_object (self->sctp_association, "notify::state",
      G_CALLBACK (on_sctp_association_state_changed), self, 0);

  g_object_bind_property (self, "remote-sctp-port", self->sctp_association,
      "remote-port", G_BINDING_SYNC_CREATE);

  g_object_bind_property (self, "use-sock-stream", self->sctp_association,
      "use-sock-stream", G_BINDING_SYNC_CREATE);

  gst_sctp_association_set_on_packet_out (self->sctp_association,
      on_sctp_packet_out, self);

  return TRUE;
error:
  return FALSE;
}

static void
on_sctp_association_state_changed (GstSctpAssociation * sctp_association,
    GParamSpec * pspec, GstSctpEnc * self)
{
  gint state;

  g_object_get (sctp_association, "state", &state, NULL);
  switch (state) {
    case GST_SCTP_ASSOCIATION_STATE_NEW:
      break;
    case GST_SCTP_ASSOCIATION_STATE_READY:
      gst_sctp_association_start (sctp_association);
      break;
    case GST_SCTP_ASSOCIATION_STATE_CONNECTING:
      break;
    case GST_SCTP_ASSOCIATION_STATE_CONNECTED:
      g_signal_emit_by_name (self, "sctp-association-established", TRUE);
      break;
    case GST_SCTP_ASSOCIATION_STATE_DISCONNECTING:
      g_signal_emit (self, signals[SIGNAL_SCTP_ASSOCIATION_ESTABLISHED], 0,
          FALSE);
      break;
    case GST_SCTP_ASSOCIATION_STATE_DISCONNECTED:
      break;
    case GST_SCTP_ASSOCIATION_STATE_ERROR:
      break;
  }
}

static void
data_queue_item_free (GstDataQueueItem * item)
{
  if (item->object)
    gst_mini_object_unref (item->object);
  g_free (item);
}

static void
on_sctp_packet_out (GstSctpAssociation * _association, const guint8 * buf,
    gsize length, gpointer user_data)
{
  GstSctpEnc *self = user_data;
  GstBuffer *gstbuf;
  GstDataQueueItem *item;
  GList *pending_pads, *l;
  GstSctpEncPad *sctpenc_pad;

  gstbuf = gst_buffer_new_wrapped (g_memdup (buf, length), length);

  item = g_new0 (GstDataQueueItem, 1);
  item->object = GST_MINI_OBJECT (gstbuf);
  item->size = length;
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) data_queue_item_free;

  if (!gst_data_queue_push (self->outbound_sctp_packet_queue, item)) {
    item->destroy (item);
    GST_DEBUG_OBJECT (self, "Failed to push item because we're flushing");
  }

  /* Wake up pads in the order they waited, oldest pad first */
  GST_OBJECT_LOCK (self);
  pending_pads = NULL;
  while ((sctpenc_pad = g_queue_pop_tail (&self->pending_pads))) {
    pending_pads = g_list_prepend (pending_pads, sctpenc_pad);
  }
  GST_OBJECT_UNLOCK (self);

  for (l = pending_pads; l; l = l->next) {
    sctpenc_pad = l->data;
    g_mutex_lock (&sctpenc_pad->lock);
    g_cond_signal (&sctpenc_pad->cond);
    g_mutex_unlock (&sctpenc_pad->lock);
  }
  g_list_free (pending_pads);
}

static void
stop_srcpad_task (GstPad * pad, GstSctpEnc * self)
{
  gst_data_queue_set_flushing (self->outbound_sctp_packet_queue, TRUE);
  gst_data_queue_flush (self->outbound_sctp_packet_queue);
  gst_pad_stop_task (pad);
}

static void
remove_sinkpad (const GValue * item, gpointer user_data)
{
  GstSctpEncPad *sctpenc_pad = g_value_get_object (item);
  GstSctpEnc *self = user_data;

  gst_sctp_enc_release_pad (GST_ELEMENT (self), GST_PAD (sctpenc_pad));
}

static void
sctpenc_cleanup (GstSctpEnc * self)
{
  GstIterator *it;

  /* FIXME: make this threadsafe */
  /* gst_sctp_association_set_on_packet_out (self->sctp_association, NULL, NULL); */

  g_signal_handler_disconnect (self->sctp_association,
      self->signal_handler_state_changed);
  stop_srcpad_task (self->src_pad, self);
  gst_sctp_association_force_close (self->sctp_association);
  g_object_unref (self->sctp_association);
  self->sctp_association = NULL;

  it = gst_element_iterate_sink_pads (GST_ELEMENT (self));
  while (gst_iterator_foreach (it, remove_sinkpad, self) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);
  gst_iterator_free (it);
  g_queue_clear (&self->pending_pads);
}

static void
get_config_from_caps (const GstCaps * caps, gboolean * ordered,
    GstSctpAssociationPartialReliability * reliability,
    guint32 * reliability_param, guint32 * ppid, gboolean * ppid_available)
{
  GstStructure *s;
  guint i, n;

  *ordered = TRUE;
  *reliability = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_NONE;
  *reliability_param = 0;
  *ppid_available = FALSE;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (caps, i);
    if (gst_structure_has_field (s, "ordered")) {
      const GValue *v = gst_structure_get_value (s, "ordered");
      *ordered = g_value_get_boolean (v);
    }
    if (gst_structure_has_field (s, "partially-reliability")) {
      const GValue *v = gst_structure_get_value (s, "partially-reliability");
      const gchar *reliability_string = g_value_get_string (v);

      if (!g_strcmp0 (reliability_string, "none"))
        *reliability = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_NONE;
      else if (!g_strcmp0 (reliability_string, "ttl"))
        *reliability = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_TTL;
      else if (!g_strcmp0 (reliability_string, "buf"))
        *reliability = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_BUF;
      else if (!g_strcmp0 (reliability_string, "rtx"))
        *reliability = GST_SCTP_ASSOCIATION_PARTIAL_RELIABILITY_RTX;
    }
    if (gst_structure_has_field (s, "reliability-parameter")) {
      const GValue *v = gst_structure_get_value (s, "reliability-parameter");
      *reliability_param = g_value_get_uint (v);
    }
    if (gst_structure_has_field (s, "ppid")) {
      const GValue *v = gst_structure_get_value (s, "ppid");
      *ppid = g_value_get_uint (v);
      *ppid_available = TRUE;
    }
  }
}

static guint64
on_get_stream_bytes_sent (GstSctpEnc * self, guint stream_id)
{
  gchar *pad_name;
  GstPad *pad;
  GstSctpEncPad *sctpenc_pad;
  guint64 bytes_sent;

  pad_name = g_strdup_printf ("sink_%u", stream_id);
  pad = gst_element_get_static_pad (GST_ELEMENT (self), pad_name);
  g_free (pad_name);

  if (!pad) {
    GST_DEBUG_OBJECT (self,
        "Buffered amount requested on a stream that does not exist!");
    return 0;
  }

  sctpenc_pad = GST_SCTP_ENC_PAD (pad);

  g_mutex_lock (&sctpenc_pad->lock);
  bytes_sent = sctpenc_pad->bytes_sent;
  g_mutex_unlock (&sctpenc_pad->lock);

  gst_object_unref (sctpenc_pad);

  return bytes_sent;
}
