/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gstcodecseiinserter.h"
#include <gst/video/video-sei.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_codec_sei_inserter_debug);
#define GST_CAT_DEFAULT gst_codec_sei_inserter_debug

/**
 * GstCodecSEIInsertMetaOrder:
 *
 * Since: 1.26
 */
#define GST_TYPE_CODEC_SEI_INSERT_META_ORDER (gst_codec_sei_insert_meta_order_mode_get_type())
static GType
gst_codec_sei_insert_meta_order_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue order_types[] = {
    {GST_CODEC_SEI_INSERT_META_ORDER_DECODE, "Decode", "decode"},
    {GST_CODEC_SEI_INSERT_META_ORDER_DISPLAY, "Display", "display"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter ((gsize *) & type)) {
    GType tmp = g_enum_register_static ("GstCodecSEIInsertMetaOrder",
        order_types);
    g_once_init_leave ((gsize *) & type, tmp);
  }

  return type;
}

/**
 * GstCodecSEIInsertType:
 *
 * Since: 1.30
 */
#define GST_TYPE_CODEC_SEI_INSERT_TYPE (gst_codec_sei_insert_type_get_type())
GType
gst_codec_sei_insert_type_get_type (void)
{
  static GType type = 0;
  static const GFlagsValue sei_types[] = {
    {GST_CODEC_SEI_INSERT_CC, "Closed caption", "cc"},
    {GST_CODEC_SEI_INSERT_UNREGISTERED, "Unregistered user data",
        "unregistered"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter ((gsize *) & type)) {
    GType tmp = g_flags_register_static ("GstCodecSEIInsertType",
        sei_types);
    g_once_init_leave ((gsize *) & type, tmp);
  }

  return type;
}

enum
{
  PROP_0,
  PROP_CAPTION_META_ORDER,
  PROP_REMOVE_CAPTION_META,
  PROP_SEI_TYPES,
  PROP_REMOVE_SEI_UNREGISTERED_META,
};

#define DEFAULT_CAPTION_META_ORDER GST_CODEC_SEI_INSERT_META_ORDER_DECODE
#define DEFAULT_REMOVE_CAPTION_META FALSE
/* Default to CC-only for base class (ccinserter behavior).
 * SEIInserter subclasses override this to GST_CODEC_SEI_INSERT_ALL */
#define DEFAULT_SEI_TYPES GST_CODEC_SEI_INSERT_CC
#define DEFAULT_REMOVE_SEI_UNREGISTERED_META FALSE

struct _GstCodecSEIInserterPrivate
{
  GMutex lock;

  GList *current_frame_events;
  GPtrArray *sei_metas;
  GstClockTime latency;

  GstCodecSEIInsertMetaOrder meta_order;
  gboolean remove_meta;
  GstCodecSEIInsertType sei_types;
  gboolean remove_sei_unregistered_meta;
};

static void gst_codec_sei_inserter_class_init (GstCodecSEIInserterClass *
    klass);
static void gst_codec_sei_inserter_init (GstCodecSEIInserter * self,
    GstCodecSEIInserterClass * klass);
static void gst_codec_sei_inserter_finalize (GObject * object);
static void gst_codec_sei_inserter_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_codec_sei_inserter_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_codec_sei_inserter_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_codec_sei_inserter_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_codec_sei_inserter_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_codec_sei_inserter_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstCaps *gst_codec_sei_inserter_get_caps (GstCodecSEIInserter * self,
    GstCaps * filter);
static GstStateChangeReturn gst_codec_sei_inserter_change_state (GstElement *
    element, GstStateChange transition);
static void gst_codec_sei_inserter_reset (GstCodecSEIInserter * self);
static void gst_codec_sei_inserter_drain (GstCodecSEIInserter * self);

static GTypeClass *parent_class = NULL;
static gint private_offset = 0;

/**
 * GstCodecSEIInserter:
 *
 * Since: 1.26
 */

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_codec_sei_inserter_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstCodecSEIInserterClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_codec_sei_inserter_class_init,
      NULL,
      NULL,
      sizeof (GstCodecSEIInserter),
      0,
      (GInstanceInitFunc) gst_codec_sei_inserter_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstCodecSEIInserter", &info, G_TYPE_FLAG_ABSTRACT);

    private_offset = g_type_add_instance_private (_type,
        sizeof (GstCodecSEIInserterPrivate));

    g_once_init_leave (&type, _type);
  }
  return type;
}

static inline GstCodecSEIInserterPrivate *
gst_codec_sei_inserter_get_instance_private (GstCodecSEIInserter * self)
{
  return (G_STRUCT_MEMBER_P (self, private_offset));
}

static void
gst_codec_sei_inserter_class_init (GstCodecSEIInserterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  if (private_offset)
    g_type_class_adjust_private_offset (klass, &private_offset);

  object_class->set_property = gst_codec_sei_inserter_set_property;
  object_class->get_property = gst_codec_sei_inserter_get_property;
  object_class->finalize = gst_codec_sei_inserter_finalize;

  g_object_class_install_property (object_class, PROP_CAPTION_META_ORDER,
      g_param_spec_enum ("caption-meta-order", "Caption Meta Order",
          "Order of caption metas attached on buffers. In case of \"display\" order, "
          "inserter will reorder captions to decoding order",
          GST_TYPE_CODEC_SEI_INSERT_META_ORDER, DEFAULT_CAPTION_META_ORDER,
          GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_REMOVE_CAPTION_META,
      g_param_spec_boolean ("remove-caption-meta", "Remove Caption Meta",
          "Remove caption meta from outgoing video buffers", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_codec_sei_inserter_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_codec_sei_inserter_debug, "codecseiinserter", 0,
      "codecseiinserter");

  gst_type_mark_as_plugin_api (GST_TYPE_CODEC_SEI_INSERTER, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_CODEC_SEI_INSERT_META_ORDER, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_CODEC_SEI_INSERT_TYPE, 0);
}

static void
gst_codec_sei_inserter_init (GstCodecSEIInserter * self,
    GstCodecSEIInserterClass * klass)
{
  GstCodecSEIInserterPrivate *priv;
  GstPadTemplate *template;

  self->priv = priv = gst_codec_sei_inserter_get_instance_private (self);

  template = gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass),
      "sink");
  self->sinkpad = gst_pad_new_from_template (template, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_codec_sei_inserter_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_codec_sei_inserter_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_codec_sei_inserter_sink_query));

  GST_PAD_SET_PROXY_SCHEDULING (self->sinkpad);
  GST_PAD_SET_ACCEPT_INTERSECT (self->sinkpad);
  GST_PAD_SET_ACCEPT_TEMPLATE (self->sinkpad);
  GST_PAD_SET_PROXY_CAPS (self->sinkpad);

  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  template = gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass),
      "src");
  self->srcpad = gst_pad_new_from_template (template, "src");
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_codec_sei_inserter_src_query));
  GST_PAD_SET_PROXY_SCHEDULING (self->srcpad);

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  g_mutex_init (&priv->lock);
  priv->meta_order = DEFAULT_CAPTION_META_ORDER;
  priv->remove_meta = DEFAULT_REMOVE_CAPTION_META;
  priv->sei_types = DEFAULT_SEI_TYPES;
  priv->remove_sei_unregistered_meta = DEFAULT_REMOVE_SEI_UNREGISTERED_META;
  priv->sei_metas = g_ptr_array_new ();
}

static void
gst_codec_sei_inserter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (object);
  GstCodecSEIInserterPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);

  switch (prop_id) {
    case PROP_CAPTION_META_ORDER:
      priv->meta_order = g_value_get_enum (value);
      break;
    case PROP_REMOVE_CAPTION_META:
      priv->remove_meta = g_value_get_boolean (value);
      break;
    case PROP_SEI_TYPES:
      priv->sei_types = g_value_get_flags (value);
      break;
    case PROP_REMOVE_SEI_UNREGISTERED_META:
      priv->remove_sei_unregistered_meta = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&priv->lock);
}

static void
gst_codec_sei_inserter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (object);
  GstCodecSEIInserterPrivate *priv = self->priv;

  g_mutex_lock (&priv->lock);

  switch (prop_id) {
    case PROP_CAPTION_META_ORDER:
      g_value_set_enum (value, priv->meta_order);
      break;
    case PROP_REMOVE_CAPTION_META:
      g_value_set_boolean (value, priv->remove_meta);
      break;
    case PROP_SEI_TYPES:
      g_value_set_flags (value, priv->sei_types);
      break;
    case PROP_REMOVE_SEI_UNREGISTERED_META:
      g_value_set_boolean (value, priv->remove_sei_unregistered_meta);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&priv->lock);
}

static void
gst_codec_sei_inserter_finalize (GObject * object)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (object);
  GstCodecSEIInserterPrivate *priv = self->priv;

  g_mutex_clear (&priv->lock);
  g_ptr_array_unref (priv->sei_metas);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_codec_sei_inserter_flush_events (GstCodecSEIInserter * self,
    GList ** events)
{
  GList *iter;

  for (iter = *events; iter; iter = g_list_next (iter)) {
    GstEvent *ev = GST_EVENT (iter->data);

    if (GST_EVENT_IS_STICKY (ev) && GST_EVENT_TYPE (ev) != GST_EVENT_EOS &&
        GST_EVENT_TYPE (ev) != GST_EVENT_SEGMENT) {
      gst_pad_store_sticky_event (self->srcpad, ev);
    }

    gst_event_unref (ev);
  }

  g_clear_pointer (events, g_list_free);
}

static void
gst_codec_sei_inserter_flush (GstCodecSEIInserter * self)
{
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);
  GstCodecSEIInserterPrivate *priv = self->priv;
  GstVideoCodecFrame *frame;

  klass->drain (self);

  while ((frame = klass->pop (self)) != NULL) {
    gst_codec_sei_inserter_flush_events (self, &frame->events);
    gst_video_codec_frame_unref (frame);
  }

  gst_codec_sei_inserter_flush_events (self, &priv->current_frame_events);
}

static void
gst_codec_sei_insert_update_latency (GstCodecSEIInserter * self,
    GstClockTime latency)
{
  GstCodecSEIInserterPrivate *priv = self->priv;
  gboolean post_msg = FALSE;

  if (!GST_CLOCK_TIME_IS_VALID (latency))
    return;

  g_mutex_lock (&priv->lock);
  if (GST_CLOCK_TIME_IS_VALID (latency) && priv->latency < latency) {
    priv->latency = latency;
    post_msg = TRUE;
  }
  g_mutex_unlock (&priv->lock);

  if (post_msg) {
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_message_new_latency (GST_OBJECT_CAST (self)));
  }
}

static gboolean
gst_codec_sei_inserter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (parent);
  GstCodecSEIInserterPrivate *priv = self->priv;
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);
  gboolean forward = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstClockTime latency = 0;

      gst_event_parse_caps (event, &caps);
      if (!klass->set_caps (self, caps, &latency)) {
        GST_ERROR_OBJECT (self, "Couldn't set caps");
        gst_event_unref (event);
        return FALSE;
      }

      gst_codec_sei_insert_update_latency (self, latency);

      if (klass->get_num_buffered (self) == 0) {
        GST_DEBUG_OBJECT (self, "No buffered frame, forward caps immediately");
        forward = TRUE;
      }
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;
      gst_event_copy_segment (event, &segment);
      if (segment.rate < 0) {
        GST_ERROR_OBJECT (self, "Negative rate is not supported");
        gst_event_unref (event);
        return FALSE;
      }

      if (klass->get_num_buffered (self) == 0) {
        GST_DEBUG_OBJECT (self,
            "No buffered frame, forward segment immediately");
        forward = TRUE;
      }
      break;
    }
    case GST_EVENT_STREAM_START:
    case GST_EVENT_EOS:
      gst_codec_sei_inserter_drain (self);
      if (priv->current_frame_events) {
        GList *iter;

        for (iter = priv->current_frame_events; iter; iter = g_list_next (iter))
          gst_pad_push_event (self->srcpad, GST_EVENT (iter->data));

        g_clear_pointer (&priv->current_frame_events, g_list_free);
      }
      forward = TRUE;
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_codec_sei_inserter_flush (self);
      forward = TRUE;
      break;
    default:
      break;
  }

  if (!GST_EVENT_IS_SERIALIZED (event) || forward) {
    return gst_pad_event_default (pad, parent, event);
  }

  /* Store event to serialize queued frames */
  priv->current_frame_events = g_list_append (priv->current_frame_events,
      event);

  return TRUE;
}

static gboolean
remove_caption_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  if ((*meta)->info->api == GST_VIDEO_CAPTION_META_API_TYPE)
    *meta = NULL;

  return TRUE;
}

static gboolean
copy_caption_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  GstVideoCaptionMeta *cc_meta;
  GstBuffer *outbuf = GST_BUFFER (user_data);

  if ((*meta)->info->api != GST_VIDEO_CAPTION_META_API_TYPE)
    return TRUE;

  cc_meta = (GstVideoCaptionMeta *) (*meta);
  gst_buffer_add_video_caption_meta (outbuf, cc_meta->caption_type,
      cc_meta->data, cc_meta->size);

  return TRUE;
}

static gboolean
extract_caption_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  GstVideoCaptionMeta *cc_meta;
  GPtrArray *array = user_data;

  if ((*meta)->info->api != GST_VIDEO_CAPTION_META_API_TYPE)
    return TRUE;

  cc_meta = (GstVideoCaptionMeta *) (*meta);
  /* TODO: Support conversion to cea708 cc_data? */
  if (cc_meta->caption_type == GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
    g_ptr_array_add (array, cc_meta);

  return TRUE;
}

static gboolean
remove_sei_unregistered_meta (GstBuffer * buffer, GstMeta ** meta,
    gpointer user_data)
{
  if ((*meta)->info->api == GST_VIDEO_SEI_USER_DATA_UNREGISTERED_META_API_TYPE)
    *meta = NULL;

  return TRUE;
}

static gboolean
copy_sei_unregistered_meta (GstBuffer * buffer, GstMeta ** meta,
    gpointer user_data)
{
  GstVideoSEIUserDataUnregisteredMeta *sei_meta;
  GstBuffer *outbuf = GST_BUFFER (user_data);

  if ((*meta)->info->api != GST_VIDEO_SEI_USER_DATA_UNREGISTERED_META_API_TYPE)
    return TRUE;

  sei_meta = (GstVideoSEIUserDataUnregisteredMeta *) (*meta);
  gst_buffer_add_video_sei_user_data_unregistered_meta (outbuf, sei_meta->uuid,
      sei_meta->data, sei_meta->size);

  return TRUE;
}

static gboolean
extract_sei_unregistered_meta (GstBuffer * buffer, GstMeta ** meta,
    gpointer user_data)
{
  GstVideoSEIUserDataUnregisteredMeta *sei_meta;
  GPtrArray *array = user_data;

  if ((*meta)->info->api != GST_VIDEO_SEI_USER_DATA_UNREGISTERED_META_API_TYPE)
    return TRUE;

  sei_meta = (GstVideoSEIUserDataUnregisteredMeta *) (*meta);
  g_ptr_array_add (array, sei_meta);

  return TRUE;
}

static GstFlowReturn
gst_codec_sei_inserter_output_frame (GstCodecSEIInserter * self,
    GstVideoCodecFrame * frame)
{
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);
  GstCodecSEIInserterPrivate *priv = self->priv;
  GList *iter;
  GstFlowReturn ret;
  GstBuffer *output;
  GstBuffer *caption_source;
  gboolean reordered = FALSE;

  for (iter = frame->events; iter; iter = g_list_next (iter)) {
    GstEvent *event = GST_EVENT (iter->data);
    gst_pad_push_event (self->srcpad, event);
  }

  g_clear_pointer (&frame->events, g_list_free);

  output = gst_buffer_copy (frame->input_buffer);
  g_mutex_lock (&priv->lock);
  caption_source = frame->input_buffer;
  if (priv->meta_order == GST_CODEC_SEI_INSERT_META_ORDER_DISPLAY &&
      frame->output_buffer) {
    caption_source = frame->output_buffer;
    if (frame->output_buffer != frame->input_buffer)
      reordered = TRUE;
  }

  /* Remove caption meta from outgoing buffer if requested or
   * caption is reordered */
  if (priv->remove_meta || reordered)
    gst_buffer_foreach_meta (output, remove_caption_meta, NULL);

  /* Attach meta again if meta was removed because of reordering */
  if (!priv->remove_meta && reordered)
    gst_buffer_foreach_meta (caption_source, copy_caption_meta, output);

  /* Remove unregistered SEI meta from outgoing buffer if requested or
   * meta source is reordered */
  if (priv->remove_sei_unregistered_meta || reordered)
    gst_buffer_foreach_meta (output, remove_sei_unregistered_meta, NULL);

  /* Attach meta again if meta was removed because of reordering */
  if (!priv->remove_sei_unregistered_meta && reordered)
    gst_buffer_foreach_meta (caption_source, copy_sei_unregistered_meta,
        output);

  g_ptr_array_set_size (priv->sei_metas, 0);

  /* Collect metas based on sei-types property */
  if (priv->sei_types & GST_CODEC_SEI_INSERT_CC) {
    gst_buffer_foreach_meta (caption_source,
        extract_caption_meta, priv->sei_metas);
  }

  if (priv->sei_types & GST_CODEC_SEI_INSERT_UNREGISTERED) {
    gst_buffer_foreach_meta (caption_source,
        extract_sei_unregistered_meta, priv->sei_metas);
  }

  output = klass->insert_sei (self, output, priv->sei_metas);
  g_mutex_unlock (&priv->lock);

  gst_video_codec_frame_unref (frame);

  GST_LOG_OBJECT (self, "Output %" GST_PTR_FORMAT, output);

  ret = gst_pad_push (self->srcpad, output);

  return ret;
}

static void
gst_codec_sei_inserter_drain (GstCodecSEIInserter * self)
{
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);
  GstVideoCodecFrame *frame;

  klass->drain (self);

  while ((frame = klass->pop (self)) != NULL)
    gst_codec_sei_inserter_output_frame (self, frame);
}

static GstFlowReturn
gst_codec_sei_inserter_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (parent);
  GstCodecSEIInserterPrivate *priv = self->priv;
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);
  GstVideoCodecFrame *frame;
  GstClockTime latency = 0;

  GST_LOG_OBJECT (self, "Handle %" GST_PTR_FORMAT, buffer);

  frame = g_new0 (GstVideoCodecFrame, 1);
  frame->ref_count = 1;
  frame->input_buffer = buffer;
  frame->events = priv->current_frame_events;
  priv->current_frame_events = NULL;

  gst_video_codec_frame_ref (frame);
  if (!klass->push (self, frame, &latency)) {
    GST_ERROR_OBJECT (self, "Couldn't process frame");
    priv->current_frame_events = frame->events;
    frame->events = NULL;
    gst_video_codec_frame_unref (frame);
    /* TODO: return error in case of too many decoding error */
    return GST_FLOW_OK;
  }

  gst_video_codec_frame_unref (frame);
  gst_codec_sei_insert_update_latency (self, latency);

  while ((frame = klass->pop (self)) != NULL) {
    GstFlowReturn ret = gst_codec_sei_inserter_output_frame (self, frame);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  return GST_FLOW_OK;
}

static GstCaps *
gst_codec_sei_inserter_get_caps (GstCodecSEIInserter * self, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res, *tmp, *pcopy;

  templ = gst_pad_get_pad_template_caps (self->sinkpad);
  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);

    peercaps = gst_pad_peer_query_caps (self->srcpad, fcopy);
    gst_caps_unref (fcopy);
  } else {
    peercaps = gst_pad_peer_query_caps (self->srcpad, NULL);
  }

  pcopy = gst_caps_copy (peercaps);

  res = gst_caps_intersect_full (pcopy, templ, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (pcopy);
  gst_caps_unref (templ);

  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (res, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = tmp;
  }

  /* Try if we can put the downstream caps first */
  pcopy = gst_caps_copy (peercaps);
  tmp = gst_caps_intersect_full (pcopy, res, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (pcopy);
  if (!gst_caps_is_empty (tmp))
    res = gst_caps_merge (tmp, res);
  else
    gst_caps_unref (tmp);

  gst_caps_unref (peercaps);
  return res;
}

static gboolean
gst_codec_sei_inserter_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *caps, *filter;

      gst_query_parse_caps (query, &filter);
      caps = gst_codec_sei_inserter_get_caps (self, filter);
      GST_LOG_OBJECT (self, "sink getcaps returning caps %" GST_PTR_FORMAT,
          caps);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_codec_sei_inserter_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (parent);
  GstCodecSEIInserterPrivate *priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean ret;

      ret = gst_pad_peer_query (self->sinkpad, query);
      if (ret) {
        GstClockTime min, max;
        gboolean live;

        gst_query_parse_latency (query, &live, &min, &max);

        g_mutex_lock (&priv->lock);
        if (GST_CLOCK_TIME_IS_VALID (priv->latency)) {
          min += priv->latency;

          if (GST_CLOCK_TIME_IS_VALID (max))
            max += priv->latency;
        }
        g_mutex_unlock (&priv->lock);

        gst_query_set_latency (query, live, min, max);
      }
      return ret;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static void
gst_codec_sei_inserter_reset (GstCodecSEIInserter * self)
{
  GstCodecSEIInserterPrivate *priv = self->priv;

  if (priv->current_frame_events) {
    g_list_free_full (priv->current_frame_events,
        (GDestroyNotify) gst_event_unref);
    priv->current_frame_events = NULL;
  }

  priv->latency = 0;
}

static gboolean
gst_codec_sei_inserter_start (GstCodecSEIInserter * self)
{
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);
  GstCodecSEIInserterPrivate *priv = self->priv;

  gst_codec_sei_inserter_reset (self);

  if (klass->start)
    return klass->start (self, priv->meta_order);

  return TRUE;
}

static gboolean
gst_codec_sei_inserter_stop (GstCodecSEIInserter * self)
{
  GstCodecSEIInserterClass *klass = GST_CODEC_SEI_INSERTER_GET_CLASS (self);

  gst_codec_sei_inserter_reset (self);

  if (klass->stop)
    return klass->stop (self);

  return TRUE;
}

static GstStateChangeReturn
gst_codec_sei_inserter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstCodecSEIInserter *self = GST_CODEC_SEI_INSERTER (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_codec_sei_inserter_start (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_codec_sei_inserter_stop (self);
      break;
    default:
      break;
  }

  return ret;
}

void
gst_codec_sei_inserter_set_sei_types (GstCodecSEIInserter * inserter,
    GstCodecSEIInsertType sei_types)
{
  GstCodecSEIInserterPrivate *priv;

  g_return_if_fail (GST_IS_CODEC_SEI_INSERTER (inserter));

  priv = inserter->priv;

  g_mutex_lock (&priv->lock);
  priv->sei_types = sei_types;
  g_mutex_unlock (&priv->lock);
}

GstCodecSEIInsertType
gst_codec_sei_inserter_get_sei_types (GstCodecSEIInserter * inserter)
{
  GstCodecSEIInserterPrivate *priv;
  GstCodecSEIInsertType ret;

  g_return_val_if_fail (GST_IS_CODEC_SEI_INSERTER (inserter),
      GST_CODEC_SEI_INSERT_CC);

  priv = inserter->priv;

  g_mutex_lock (&priv->lock);
  ret = priv->sei_types;
  g_mutex_unlock (&priv->lock);

  return ret;
}

void
gst_codec_sei_inserter_set_remove_sei_unregistered_meta (GstCodecSEIInserter *
    inserter, gboolean remove)
{
  GstCodecSEIInserterPrivate *priv;

  g_return_if_fail (GST_IS_CODEC_SEI_INSERTER (inserter));

  priv = inserter->priv;

  g_mutex_lock (&priv->lock);
  priv->remove_sei_unregistered_meta = remove;
  g_mutex_unlock (&priv->lock);
}

gboolean
gst_codec_sei_inserter_get_remove_sei_unregistered_meta (GstCodecSEIInserter *
    inserter)
{
  GstCodecSEIInserterPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (GST_IS_CODEC_SEI_INSERTER (inserter), FALSE);

  priv = inserter->priv;

  g_mutex_lock (&priv->lock);
  ret = priv->remove_sei_unregistered_meta;
  g_mutex_unlock (&priv->lock);

  return ret;
}
