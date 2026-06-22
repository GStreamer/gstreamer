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

/**
 * SECTION:element-h265ccinserter
 * @title: h265ccinserter
 *
 * Extracts closed caption metas from buffer and inserts them as SEI messages.
 *
 * For a more generic element that also supports unregistered SEI messages,
 * see #h265seiinserter.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0.exe filesrc location=video.mp4 ! parsebin name=p ! h265parse ! \
 *   queue ! cccombiner name=c ! \
 *   h265ccinserter remove-caption-meta=true caption-meta-order=display ! \
 *   h265parse ! avdec_h265 ! videoconvert ! cea608overlay ! queue ! autovideosink \
 *   filesrc location=caption.mcc ! mccparse ! ccconverter ! \
 *   closedcaption/x-cea-708,format=(string)cc_data ! queue ! c.caption
 * ```
 *
 * Above pipeline inserts closed caption data to already encoded H.265 stream
 * and renders. Because mccparse outputs caption data in display order,
 * "caption-meta-order=display" property is required in this example.
 *
 * Since: 1.26
 */

/**
 * SECTION:element-h265seiinserter
 * @title: h265seiinserter
 *
 * Extracts SEI-related metas from buffer and inserts SEI messages.
 * Supports closed caption (GstVideoCaptionMeta) and unregistered user data
 * (GstVideoSEIUserDataUnregisteredMeta) SEI messages.
 *
 * Since: 1.30
 */

/**
 * SECTION:element-h265timestamper
 * @title: h265timestamper
 * @short_description: A timestamp correction element for H.265 streams
 *
 * `h265timestamper` updates the DTS (Decoding Time Stamp) of each frame
 * based on H.265 SPS codec setup data, specifically the frame reordering
 * information written in the SPS indicating the maximum number of B-frames
 * allowed.
 *
 * In order to determine the DTS of each frame, this element may need to hold
 * back a few frames in case the codec data indicates that frame reordering is
 * allowed for the given stream. That means this element may introduce additional
 * latency for the DTS decision.
 *
 * This element can be useful if downstream elements require correct DTS
 * information but upstream elements either do not provide it at all or the
 * upstream DTS information is unreliable.
 *
 * For example, mp4 muxers typically require both DTS and PTS on the input
 * buffers, but in case where the input H.265 data comes from Matroska files or
 * RTP/RTSP streams DTS timestamps may be absent and this element may need to
 * be used to clean up the DTS timestamps before handing it to the mp4 muxer.
 *
 * This is particularly the case where the H.265 stream contains B-frames
 * (i.e. frame reordering is required), as streams without correct DTS information
 * will confuse the muxer element and will result in unexpected (or bogus)
 * duration/framerate/timestamp values in the muxed container stream.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=video.mkv ! matroskademux ! h265parse ! h265timestamper ! mp4mux ! filesink location=output.mp4
 * ```
 *
 * Since: 1.22
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth265seiinserter.h"
#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/video-sei.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_h265_base_sei_inserter_debug);
#define GST_CAT_DEFAULT gst_h265_base_sei_inserter_debug

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, alignment=(string) au"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, alignment=(string) au"));

static void gst_h265_base_sei_inserter_finalize (GObject * object);

static gboolean gst_h265_base_sei_inserter_start (GstCodecSEIInserter *
    inserter, gboolean need_reorder);
static gboolean gst_h265_base_sei_inserter_stop (GstCodecSEIInserter *
    inserter);
static gboolean gst_h265_base_sei_inserter_set_caps (GstCodecSEIInserter *
    inserter, GstCaps * caps, GstClockTime * latency);
static guint gst_h265_base_sei_inserter_get_num_buffered (GstCodecSEIInserter *
    inserter);
static gboolean gst_h265_base_sei_inserter_push (GstCodecSEIInserter * inserter,
    GstVideoCodecFrame * frame, GstClockTime * latency);
static GstVideoCodecFrame *gst_h265_base_sei_inserter_pop (GstCodecSEIInserter *
    inserter);
static void gst_h265_base_sei_inserter_drain (GstCodecSEIInserter * inserter);
static GstBuffer *gst_h265_base_sei_inserter_insert_sei (GstCodecSEIInserter *
    inserter, GstBuffer * buffer, GPtrArray * metas);

#define gst_h265_base_sei_inserter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstH265BaseSEIInserter,
    gst_h265_base_sei_inserter, GST_TYPE_CODEC_SEI_INSERTER);

/**
 * GstH265BaseSEIInserter:
 *
 * Since: 1.30
 */
static void
gst_h265_base_sei_inserter_class_init (GstH265BaseSEIInserterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCodecSEIInserterClass *inserter_class =
      GST_CODEC_SEI_INSERTER_CLASS (klass);

  object_class->finalize = gst_h265_base_sei_inserter_finalize;

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);
  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  inserter_class->start = GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_start);
  inserter_class->stop = GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_stop);
  inserter_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_set_caps);
  inserter_class->get_num_buffered =
      GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_get_num_buffered);
  inserter_class->push = GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_push);
  inserter_class->pop = GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_pop);
  inserter_class->drain = GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_drain);
  inserter_class->insert_sei =
      GST_DEBUG_FUNCPTR (gst_h265_base_sei_inserter_insert_sei);

  GST_DEBUG_CATEGORY_INIT (gst_h265_base_sei_inserter_debug, "h265ccinserter",
      0, "h265ccinserter");

  gst_type_mark_as_plugin_api (GST_TYPE_H265_BASE_SEI_INSERTER, 0);
}

static void
gst_h265_base_sei_inserter_init (GstH265BaseSEIInserter * self)
{
  self->sei_array = g_array_new (FALSE, FALSE, sizeof (GstH265SEIMessage));
  g_array_set_clear_func (self->sei_array, (GDestroyNotify) gst_h265_sei_free);
}

static void
gst_h265_base_sei_inserter_finalize (GObject * object)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (object);

  g_array_unref (self->sei_array);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h265_base_sei_inserter_start (GstCodecSEIInserter * inserter,
    gboolean need_reorder)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  self->reorder = gst_h265_reorder_new (need_reorder);

  return TRUE;
}

static gboolean
gst_h265_base_sei_inserter_stop (GstCodecSEIInserter * inserter)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  gst_clear_object (&self->reorder);

  return TRUE;
}

static gboolean
gst_h265_base_sei_inserter_set_caps (GstCodecSEIInserter * inserter,
    GstCaps * caps, GstClockTime * latency)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  return gst_h265_reorder_set_caps (self->reorder, caps, latency);
}

static gboolean
gst_h265_base_sei_inserter_push (GstCodecSEIInserter * inserter,
    GstVideoCodecFrame * frame, GstClockTime * latency)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  return gst_h265_reorder_push (self->reorder, frame, latency);
}

static GstVideoCodecFrame *
gst_h265_base_sei_inserter_pop (GstCodecSEIInserter * inserter)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  return gst_h265_reorder_pop (self->reorder);
}

static void
gst_h265_base_sei_inserter_drain (GstCodecSEIInserter * inserter)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  gst_h265_reorder_drain (self->reorder);
}

static guint
gst_h265_base_sei_inserter_get_num_buffered (GstCodecSEIInserter * inserter)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);

  return gst_h265_reorder_get_num_buffered (self->reorder);
}

static GstBuffer *
gst_h265_base_sei_inserter_insert_sei (GstCodecSEIInserter * inserter,
    GstBuffer * buffer, GPtrArray * metas)
{
  GstH265BaseSEIInserter *self = GST_H265_BASE_SEI_INSERTER (inserter);
  guint i;
  GstBuffer *new_buf;

  g_array_set_size (self->sei_array, 0);

  /* Process closed caption metas */
  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoCaptionMeta *cc_meta;
    GstH265SEIMessage sei;
    GstH265RegisteredUserData *rud;
    guint8 *data;

    if (meta->info->api != GST_VIDEO_CAPTION_META_API_TYPE)
      continue;

    cc_meta = (GstVideoCaptionMeta *) meta;

    if (cc_meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
      continue;

    memset (&sei, 0, sizeof (GstH265SEIMessage));
    sei.payloadType = GST_H265_SEI_REGISTERED_USER_DATA;
    rud = &sei.payload.registered_user_data;

    rud->country_code = 181;
    rud->size = cc_meta->size + 10;

    data = g_malloc (rud->size);
    memcpy (data + 9, cc_meta->data, cc_meta->size);

    data[0] = 0;                /* 16-bits itu_t_t35_provider_code */
    data[1] = 49;
    data[2] = 'G';              /* 32-bits ATSC_user_identifier */
    data[3] = 'A';
    data[4] = '9';
    data[5] = '4';
    data[6] = 3;                /* 8-bits ATSC1_data_user_data_type_code */
    /* 8-bits:
     * 1 bit process_em_data_flag (0)
     * 1 bit process_cc_data_flag (1)
     * 1 bit additional_data_flag (0)
     * 5-bits cc_count
     */
    data[7] = ((cc_meta->size / 3) & 0x1f) | 0x40;
    data[8] = 255;              /* 8 bits em_data, unused */
    data[cc_meta->size + 9] = 255;      /* 8 marker bits */

    rud->data = data;

    g_array_append_val (self->sei_array, sei);
  }

  /* Process unregistered SEI metas */
  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoSEIUserDataUnregisteredMeta *sei_meta;
    GstH265SEIMessage sei;
    GstH265UserDataUnregistered *udu;
    guint8 *data;

    if (meta->info->api != GST_VIDEO_SEI_USER_DATA_UNREGISTERED_META_API_TYPE)
      continue;

    sei_meta = (GstVideoSEIUserDataUnregisteredMeta *) meta;

    memset (&sei, 0, sizeof (GstH265SEIMessage));
    sei.payloadType = GST_H265_SEI_USER_DATA_UNREGISTERED;
    udu = &sei.payload.user_data_unregistered;

    memcpy (udu->uuid, sei_meta->uuid, 16);
    udu->size = sei_meta->size;

    data = g_malloc (sei_meta->size);
    memcpy (data, sei_meta->data, sei_meta->size);
    udu->data = data;

    g_array_append_val (self->sei_array, sei);
  }

  if (self->sei_array->len == 0)
    return buffer;

  new_buf = gst_h265_reorder_insert_sei (self->reorder,
      buffer, self->sei_array);

  g_array_set_size (self->sei_array, 0);

  if (!new_buf) {
    GST_WARNING_OBJECT (self, "Couldn't insert SEI");
    return buffer;
  }

  gst_buffer_unref (buffer);
  return new_buf;
}

enum
{
  PROP_CC_0,
  PROP_CC_DO_TIMESTAMP,
};

#define DEFAULT_DO_TIMESTAMP FALSE

struct _GstH265CCInserter
{
  GstH265BaseSEIInserter parent;
};

#define gst_h265_cc_inserter_parent_class cc_inserter_parent_class
G_DEFINE_TYPE (GstH265CCInserter,
    gst_h265_cc_inserter, GST_TYPE_H265_BASE_SEI_INSERTER);
GST_ELEMENT_REGISTER_DEFINE (h265ccinserter, "h265ccinserter",
    GST_RANK_NONE, GST_TYPE_H265_CC_INSERTER);

static void
gst_h265_cc_inserter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCodecSEIInserter *inserter = GST_CODEC_SEI_INSERTER (object);

  switch (prop_id) {
    case PROP_CC_DO_TIMESTAMP:
      gst_codec_sei_inserter_set_do_timestamp (inserter,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h265_cc_inserter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCodecSEIInserter *inserter = GST_CODEC_SEI_INSERTER (object);

  switch (prop_id) {
    case PROP_CC_DO_TIMESTAMP:
      g_value_set_boolean (value,
          gst_codec_sei_inserter_get_do_timestamp (inserter));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h265_cc_inserter_class_init (GstH265CCInserterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_h265_cc_inserter_set_property;
  object_class->get_property = gst_h265_cc_inserter_get_property;

  /**
   * GstH265CCInserter:do-timestamp:
   *
   * Recalculate DTS based on input PTS and output frame order.
   *
   * When enabled, the element ignores any DTS values present on
   * incoming frames and always derives new DTS values from the input PTS
   * and the actual output (decode) order of frames.
   *
   * Since: 1.30
   */
  g_object_class_install_property (object_class, PROP_CC_DO_TIMESTAMP,
      g_param_spec_boolean ("do-timestamp", "Do Timestamp",
          "Recalculate DTS from input PTS and output frame order",
          DEFAULT_DO_TIMESTAMP, GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "H.265 Closed Caption Inserter",
      "Codec/Video/Filter",
      "Insert closed caption SEI messages into H.265 streams",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_h265_cc_inserter_init (GstH265CCInserter * self)
{
  gst_codec_sei_inserter_set_sei_types (GST_CODEC_SEI_INSERTER (self),
      GST_CODEC_SEI_INSERT_CC);
}

/* H265 SEI Inserter - subclass that adds sei-types property */
enum
{
  PROP_0,
  PROP_SEI_TYPES,
  PROP_REMOVE_SEI_UNREGISTERED_META,
  PROP_DO_TIMESTAMP
};

struct _GstH265SEIInserter
{
  GstH265BaseSEIInserter parent;
};

static void gst_h265_sei_inserter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h265_sei_inserter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_h265_sei_inserter_parent_class sei_inserter_parent_class
G_DEFINE_TYPE (GstH265SEIInserter,
    gst_h265_sei_inserter, GST_TYPE_H265_BASE_SEI_INSERTER);
GST_ELEMENT_REGISTER_DEFINE (h265seiinserter, "h265seiinserter",
    GST_RANK_NONE, GST_TYPE_H265_SEI_INSERTER);

static void
gst_h265_sei_inserter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCodecSEIInserter *inserter = GST_CODEC_SEI_INSERTER (object);

  switch (prop_id) {
    case PROP_SEI_TYPES:
      gst_codec_sei_inserter_set_sei_types (inserter,
          g_value_get_flags (value));
      break;
    case PROP_REMOVE_SEI_UNREGISTERED_META:
      gst_codec_sei_inserter_set_remove_sei_unregistered_meta (inserter,
          g_value_get_boolean (value));
      break;
    case PROP_DO_TIMESTAMP:
      gst_codec_sei_inserter_set_do_timestamp (inserter,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h265_sei_inserter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCodecSEIInserter *inserter = GST_CODEC_SEI_INSERTER (object);

  switch (prop_id) {
    case PROP_SEI_TYPES:
      g_value_set_flags (value,
          gst_codec_sei_inserter_get_sei_types (inserter));
      break;
    case PROP_REMOVE_SEI_UNREGISTERED_META:
      g_value_set_boolean (value,
          gst_codec_sei_inserter_get_remove_sei_unregistered_meta (inserter));
      break;
    case PROP_DO_TIMESTAMP:
      g_value_set_boolean (value,
          gst_codec_sei_inserter_get_do_timestamp (inserter));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h265_sei_inserter_class_init (GstH265SEIInserterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_h265_sei_inserter_set_property;
  object_class->get_property = gst_h265_sei_inserter_get_property;

  gst_element_class_set_static_metadata (element_class,
      "H.265 SEI Inserter",
      "Codec/Video/Filter", "Insert SEI messages into H.265 streams",
      "Seungha Yang <seungha@centricular.com>");

  /**
   * GstH265SEIInserter:sei-types:
   *
   * Which SEI message types to insert.
   *
   * Since: 1.30
   */
  g_object_class_install_property (object_class,
      PROP_SEI_TYPES,
      g_param_spec_flags ("sei-types", "SEI Types",
          "Which SEI message types to insert",
          GST_TYPE_CODEC_SEI_INSERT_TYPE, GST_CODEC_SEI_INSERT_ALL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstH265SEIInserter:remove-sei-unregistered-meta:
   *
   * Remove GstVideoSEIUserDataUnregisteredMeta from outgoing video buffers.
   *
   * Since: 1.30
   */
  g_object_class_install_property (object_class,
      PROP_REMOVE_SEI_UNREGISTERED_META,
      g_param_spec_boolean ("remove-sei-unregistered-meta",
          "Remove SEI Unregistered Meta",
          "Remove SEI unregistered user data meta from outgoing video buffers",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstH265SEIInserter:do-timestamp:
   *
   * Recalculate DTS based on input PTS and output frame order.
   *
   * When enabled, the element ignores any DTS values present on
   * incoming frames and always derives new DTS values from the input PTS
   * and the actual output (decode) order of frames.
   *
   * Since: 1.30
   */
  g_object_class_install_property (object_class, PROP_DO_TIMESTAMP,
      g_param_spec_boolean ("do-timestamp", "Do Timestamp",
          "Recalculate DTS from input PTS and output frame order",
          DEFAULT_DO_TIMESTAMP, GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));
}

static void
gst_h265_sei_inserter_init (GstH265SEIInserter * self)
{
  /* Set sei_types to ALL for SEI inserter (base class defaults to CC-only) */
  gst_codec_sei_inserter_set_sei_types (GST_CODEC_SEI_INSERTER (self),
      GST_CODEC_SEI_INSERT_ALL);
}

struct _GstH265Timestamper
{
  GstH265BaseSEIInserter parent;
};

G_DEFINE_TYPE (GstH265Timestamper,
    gst_h265_timestamper, GST_TYPE_H265_BASE_SEI_INSERTER);
GST_ELEMENT_REGISTER_DEFINE (h265timestamper, "h265timestamper",
    GST_RANK_NONE, GST_TYPE_H265_TIMESTAMPER);

static void
gst_h265_timestamper_class_init (GstH265TimestamperClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "H.265 timestamper", "Codec/Video/Timestamper", "Timestamp H.265 streams",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_h265_timestamper_init (GstH265Timestamper * self)
{
  GstCodecSEIInserter *inserter = GST_CODEC_SEI_INSERTER (self);

  gst_codec_sei_inserter_set_sei_types (inserter, 0);
  gst_codec_sei_inserter_set_do_timestamp (inserter, TRUE);
}
