/* GStreamer
  * Copyright (C) 2026 Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
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
 * SECTION:element-h266seiinserter
 * @title: h266seiinserter
 *
 * Extracts SEI-related metas from buffer and inserts SEI messages.
 * Supports closed caption (GstVideoCaptionMeta), unregistered user data
 * (GstVideoSEIUserDataUnregisteredMeta) and DSC SEI messages.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth266seiinserter.h"
#include <gst/codecparsers/gsth266parser.h>
#include <gst/codecparsers/gsth274parser.h>
#include <gst/video/gstvideodscmeta.h>
#include <gst/video/video-sei.h>
#include <gst/video/gsth274.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_h266_sei_inserter_debug);
#define GST_CAT_DEFAULT gst_h266_sei_inserter_debug

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h266, alignment=(string) au"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h266, alignment=(string) au"));

static void gst_h266_sei_inserter_finalize (GObject * object);

static gboolean gst_h266_sei_inserter_start (GstCodecSEIInserter * inserter,
    gboolean need_reorder);
static gboolean gst_h266_sei_inserter_stop (GstCodecSEIInserter * inserter);
static gboolean gst_h266_sei_inserter_set_caps (GstCodecSEIInserter * inserter,
    GstCaps * caps, GstClockTime * latency);
static guint
gst_h266_sei_inserter_get_num_buffered (GstCodecSEIInserter * inserter);
static gboolean gst_h266_sei_inserter_push (GstCodecSEIInserter * inserter,
    GstVideoCodecFrame * frame, GstClockTime * latency);
static GstVideoCodecFrame *gst_h266_sei_inserter_pop (GstCodecSEIInserter *
    inserter);
static void gst_h266_sei_inserter_drain (GstCodecSEIInserter * inserter);
static GstBuffer *gst_h266_sei_inserter_insert_sei (GstCodecSEIInserter *
    inserter, GstBuffer * buffer, GPtrArray * metas);

#define gst_h266_sei_inserter_parent_class parent_class
G_DEFINE_TYPE (GstH266SEIInserter,
    gst_h266_sei_inserter, GST_TYPE_CODEC_SEI_INSERTER);
GST_ELEMENT_REGISTER_DEFINE (h266seiinserter, "h266seiinserter",
    GST_RANK_NONE, GST_TYPE_H266_SEI_INSERTER);

static void
gst_h266_sei_inserter_finalize (GObject * object)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (object);

  g_array_unref (self->sei_array);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h266_sei_inserter_start (GstCodecSEIInserter * inserter,
    gboolean need_reorder)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);
  if (need_reorder) {
    GST_ERROR_OBJECT (self,
        "H.266 SEI Inserter does not support display order caption metas.");
    return FALSE;
  }

  self->reorder = gst_h266_reorder_new (FALSE);

  return TRUE;
}

static gboolean
gst_h266_sei_inserter_stop (GstCodecSEIInserter * inserter)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);

  gst_clear_object (&self->reorder);

  return TRUE;
}

static gboolean
gst_h266_sei_inserter_set_caps (GstCodecSEIInserter * inserter, GstCaps * caps,
    GstClockTime * latency)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);

  return gst_h266_reorder_set_caps (self->reorder, caps, latency);
}

static gboolean
gst_h266_sei_inserter_push (GstCodecSEIInserter * inserter,
    GstVideoCodecFrame * frame, GstClockTime * latency)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);

  return gst_h266_reorder_push (self->reorder, frame, latency);
}

static GstVideoCodecFrame *
gst_h266_sei_inserter_pop (GstCodecSEIInserter * inserter)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);

  return gst_h266_reorder_pop (self->reorder);
}

static void
gst_h266_sei_inserter_drain (GstCodecSEIInserter * inserter)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);

  gst_h266_reorder_drain (self->reorder);
}

static guint
gst_h266_sei_inserter_get_num_buffered (GstCodecSEIInserter * inserter)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);

  return gst_h266_reorder_get_num_buffered (self->reorder);
}

static void
process_closed_caption_metas (GstH266SEIInserter * self, GPtrArray * metas)
{
  guint i;

  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoCaptionMeta *cc_meta;
    GstH266SEIMessage sei;
    GstH266RegisteredUserData *rud;
    guint8 *data;

    if (meta->info->api != GST_VIDEO_CAPTION_META_API_TYPE)
      continue;

    cc_meta = (GstVideoCaptionMeta *) meta;

    if (cc_meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
      continue;

    memset (&sei, 0, sizeof (GstH266SEIMessage));
    sei.payloadType = GST_H266_SEI_REGISTERED_USER_DATA;
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
}

static void
process_unregistered_sei_metas (GstH266SEIInserter * self, GPtrArray * metas)
{
  guint i;

  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoSEIUserDataUnregisteredMeta *sei_meta;
    GstH266SEIMessage sei;
    GstH274UserDataUnregistered *udu;
    guint8 *data;

    if (meta->info->api != GST_VIDEO_SEI_USER_DATA_UNREGISTERED_META_API_TYPE)
      continue;

    sei_meta = (GstVideoSEIUserDataUnregisteredMeta *) meta;

    memset (&sei, 0, sizeof (GstH266SEIMessage));
    sei.payloadType = GST_H266_SEI_USER_DATA_UNREGISTERED;
    udu = &sei.payload.user_data_unregistered;

    memcpy (udu->uuid, sei_meta->uuid, 16);
    udu->size = sei_meta->size;

    data = g_malloc (sei_meta->size);
    memcpy (data, sei_meta->data, sei_meta->size);
    udu->data = data;

    g_array_append_val (self->sei_array, sei);
  }
}

static void
process_dsc_initialization_metas (GstH266SEIInserter * self, GPtrArray * metas)
{
  guint i;

  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoDSCInitializationMeta *dsci_meta;
    GstH266SEIMessage sei;
    GstH274DigitallySignedContentInitialization *dsc_initialization;

    if (meta->info->api != GST_VIDEO_DSC_INITIALIZATION_META_API_TYPE)
      continue;

    dsci_meta = (GstVideoDSCInitializationMeta *) meta;

    memset (&sei, 0, sizeof (GstH266SEIMessage));
    sei.payloadType = GST_H266_SEI_DIGITALLY_SIGNED_CONTENT_INITIALIZATION;
    dsc_initialization = &sei.payload.dsc_initialization;
    gst_h274_dsc_initialization_copy (dsc_initialization,
        &dsci_meta->dsc_initialization);

    g_array_append_val (self->sei_array, sei);
  }
}

static void
process_dsc_selection_metas (GstH266SEIInserter * self, GPtrArray * metas)
{
  guint i;

  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoDSCSelectionMeta *dscs_meta;
    GstH266SEIMessage sei;
    GstH274DigitallySignedContentSelection *dsc_selection;

    if (meta->info->api != GST_VIDEO_DSC_SELECTION_META_API_TYPE)
      continue;

    dscs_meta = (GstVideoDSCSelectionMeta *) meta;

    memset (&sei, 0, sizeof (GstH266SEIMessage));
    sei.payloadType = GST_H266_SEI_DIGITALLY_SIGNED_CONTENT_SELECTION;
    dsc_selection = &sei.payload.dsc_selection;
    gst_h274_dsc_selection_copy (dsc_selection, &dscs_meta->dsc_selection);

    g_array_append_val (self->sei_array, sei);
  }
}

static void
process_dsc_verification_metas (GstH266SEIInserter * self, GPtrArray * metas)
{
  guint i;

  for (i = 0; i < metas->len; i++) {
    GstMeta *meta = g_ptr_array_index (metas, i);
    GstVideoDSCVerificationMeta *dscv_meta;
    GstH266SEIMessage sei;
    GstH274DigitallySignedContentVerification *dsc_verification;

    if (meta->info->api != GST_VIDEO_DSC_VERIFICATION_META_API_TYPE)
      continue;

    dscv_meta = (GstVideoDSCVerificationMeta *) meta;

    memset (&sei, 0, sizeof (GstH266SEIMessage));
    sei.payloadType = GST_H266_SEI_DIGITALLY_SIGNED_CONTENT_VERIFICATION;
    dsc_verification = &sei.payload.dsc_verification;
    gst_h274_dsc_verification_copy (dsc_verification,
        &dscv_meta->dsc_verification);

    g_array_append_val (self->sei_array, sei);
  }
}

static GstBuffer *
gst_h266_sei_inserter_insert_sei (GstCodecSEIInserter * inserter,
    GstBuffer * buffer, GPtrArray * metas)
{
  GstH266SEIInserter *self = GST_H266_SEI_INSERTER (inserter);
  GstBuffer *new_buf;

  g_array_set_size (self->sei_array, 0);

  process_closed_caption_metas (self, metas);
  process_unregistered_sei_metas (self, metas);
  process_dsc_initialization_metas (self, metas);
  process_dsc_selection_metas (self, metas);
  process_dsc_verification_metas (self, metas);

  if (self->sei_array->len == 0)
    return buffer;

  new_buf = gst_h266_reorder_insert_sei (self->reorder,
      buffer, self->sei_array);

  g_array_set_size (self->sei_array, 0);

  if (!new_buf) {
    GST_WARNING_OBJECT (self, "Couldn't insert SEI");
    return buffer;
  }

  gst_buffer_unref (buffer);
  return new_buf;
}

/* H266 SEI Inserter - subclass that adds sei-types property */
enum
{
  PROP_0,
  PROP_SEI_TYPES,
  PROP_REMOVE_SEI_UNREGISTERED_META,
};

static void gst_h266_sei_inserter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h266_sei_inserter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_h266_sei_inserter_set_property (GObject * object, guint prop_id,
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
    default:
      G_OBJECT_CLASS (parent_class)->set_property (object, prop_id,
          value, pspec);
      break;
  }
}

static void
gst_h266_sei_inserter_get_property (GObject * object, guint prop_id,
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
    default:
      G_OBJECT_CLASS (parent_class)->get_property (object, prop_id,
          value, pspec);
      break;
  }
}

static void
gst_h266_sei_inserter_class_init (GstH266SEIInserterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCodecSEIInserterClass *inserter_class =
      GST_CODEC_SEI_INSERTER_CLASS (klass);

  object_class->finalize = gst_h266_sei_inserter_finalize;

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);
  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  inserter_class->start = GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_start);
  inserter_class->stop = GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_stop);
  inserter_class->set_caps = GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_set_caps);
  inserter_class->get_num_buffered =
      GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_get_num_buffered);
  inserter_class->push = GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_push);
  inserter_class->pop = GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_pop);
  inserter_class->drain = GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_drain);
  inserter_class->insert_sei =
      GST_DEBUG_FUNCPTR (gst_h266_sei_inserter_insert_sei);

  GST_DEBUG_CATEGORY_INIT (gst_h266_sei_inserter_debug, "h266seiinserter", 0,
      "h266seiinserter");

  object_class->set_property = gst_h266_sei_inserter_set_property;
  object_class->get_property = gst_h266_sei_inserter_get_property;

  gst_element_class_set_static_metadata (element_class,
      "H.266 SEI Inserter",
      "Codec/Video/Filter", "Insert SEI messages into H.266 streams",
      "Diego Nieto <dnieto@fluendo.com>");

  /**
   * GstH266SEIInserter:sei-types:
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
   * GstH266SEIInserter:remove-sei-unregistered-meta:
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
}

static void
gst_h266_sei_inserter_init (GstH266SEIInserter * self)
{
  self->sei_array = g_array_new (FALSE, FALSE, sizeof (GstH266SEIMessage));
  g_array_set_clear_func (self->sei_array, (GDestroyNotify) gst_h266_sei_clear);
  /* Set sei_types to ALL for SEI inserter (base class defaults to CC-only) */
  gst_codec_sei_inserter_set_sei_types (GST_CODEC_SEI_INSERTER (self),
      GST_CODEC_SEI_INSERT_ALL);
}
