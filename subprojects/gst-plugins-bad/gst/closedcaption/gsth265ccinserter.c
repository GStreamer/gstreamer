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
 * Extracts closed caption meta from buffer and inserts closed caption SEI message
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth265ccinserter.h"
#include "gsth265reorder.h"
#include <gst/codecparsers/gsth265parser.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_h265_cc_inserter_debug);
#define GST_CAT_DEFAULT gst_h265_cc_inserter_debug

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, alignment=(string) au"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, alignment=(string) au"));

struct _GstH265CCInserter
{
  GstCodecCCInserter parent;

  GstH265Reorder *reorder;
  GArray *sei_array;
};

static void gst_h265_cc_inserter_finalize (GObject * object);

static gboolean gst_h265_cc_inserter_start (GstCodecCCInserter * inserter,
    GstCodecCCInsertMetaOrder meta_order);
static gboolean gst_h265_cc_inserter_stop (GstCodecCCInserter * inserter);
static gboolean gst_h265_cc_inserter_set_caps (GstCodecCCInserter * inserter,
    GstCaps * caps, GstClockTime * latency);
static guint
gst_h265_cc_inserter_get_num_buffered (GstCodecCCInserter * inserter);
static gboolean gst_h265_cc_inserter_push (GstCodecCCInserter * inserter,
    GstVideoCodecFrame * frame, GstClockTime * latency);
static GstVideoCodecFrame *gst_h265_cc_inserter_pop (GstCodecCCInserter *
    inserter);
static void gst_h265_cc_inserter_drain (GstCodecCCInserter * inserter);
static GstBuffer *gst_h265_cc_inserter_insert_cc (GstCodecCCInserter * inserter,
    GstBuffer * buffer, GPtrArray * metas);

#define gst_h265_cc_inserter_parent_class parent_class
G_DEFINE_TYPE (GstH265CCInserter,
    gst_h265_cc_inserter, GST_TYPE_CODEC_CC_INSERTER);
GST_ELEMENT_REGISTER_DEFINE (h265ccinserter, "h265ccinserter",
    GST_RANK_NONE, GST_TYPE_H265_CC_INSERTER);

static void
gst_h265_cc_inserter_class_init (GstH265CCInserterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCodecCCInserterClass *inserter_class = GST_CODEC_CC_INSERTER_CLASS (klass);

  object_class->finalize = gst_h265_cc_inserter_finalize;

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);
  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  gst_element_class_set_static_metadata (element_class,
      "H.265 Closed Caption Inserter",
      "Codec/Video/Filter", "Insert closed caption data to H.265 streams",
      "Seungha Yang <seungha@centricular.com>");

  inserter_class->start = GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_start);
  inserter_class->stop = GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_stop);
  inserter_class->set_caps = GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_set_caps);
  inserter_class->get_num_buffered =
      GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_get_num_buffered);
  inserter_class->push = GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_push);
  inserter_class->pop = GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_pop);
  inserter_class->drain = GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_drain);
  inserter_class->insert_cc =
      GST_DEBUG_FUNCPTR (gst_h265_cc_inserter_insert_cc);

  GST_DEBUG_CATEGORY_INIT (gst_h265_cc_inserter_debug, "h265ccinserter", 0,
      "h265ccinserter");
}

static void
gst_h265_cc_inserter_init (GstH265CCInserter * self)
{
  self->sei_array = g_array_new (FALSE, FALSE, sizeof (GstH265SEIMessage));
  g_array_set_clear_func (self->sei_array, (GDestroyNotify) gst_h265_sei_free);
}

static void
gst_h265_cc_inserter_finalize (GObject * object)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (object);

  g_array_unref (self->sei_array);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h265_cc_inserter_start (GstCodecCCInserter * inserter,
    GstCodecCCInsertMetaOrder meta_order)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);
  gboolean need_reorder = FALSE;
  if (meta_order == GST_CODEC_CC_INSERT_META_ORDER_DISPLAY)
    need_reorder = TRUE;

  self->reorder = gst_h265_reorder_new (need_reorder);

  return TRUE;
}

static gboolean
gst_h265_cc_inserter_stop (GstCodecCCInserter * inserter)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);

  gst_clear_object (&self->reorder);

  return TRUE;
}

static gboolean
gst_h265_cc_inserter_set_caps (GstCodecCCInserter * inserter, GstCaps * caps,
    GstClockTime * latency)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);

  return gst_h265_reorder_set_caps (self->reorder, caps, latency);
}

static gboolean
gst_h265_cc_inserter_push (GstCodecCCInserter * inserter,
    GstVideoCodecFrame * frame, GstClockTime * latency)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);

  return gst_h265_reorder_push (self->reorder, frame, latency);
}

static GstVideoCodecFrame *
gst_h265_cc_inserter_pop (GstCodecCCInserter * inserter)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);

  return gst_h265_reorder_pop (self->reorder);
}

static void
gst_h265_cc_inserter_drain (GstCodecCCInserter * inserter)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);

  gst_h265_reorder_drain (self->reorder);
}

static guint
gst_h265_cc_inserter_get_num_buffered (GstCodecCCInserter * inserter)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);

  return gst_h265_reorder_get_num_buffered (self->reorder);
}

static GstBuffer *
gst_h265_cc_inserter_insert_cc (GstCodecCCInserter * inserter,
    GstBuffer * buffer, GPtrArray * metas)
{
  GstH265CCInserter *self = GST_H265_CC_INSERTER (inserter);
  guint i;
  GstBuffer *new_buf;

  g_array_set_size (self->sei_array, 0);

  for (i = 0; i < metas->len; i++) {
    GstVideoCaptionMeta *meta = g_ptr_array_index (metas, i);
    GstH265SEIMessage sei;
    GstH265RegisteredUserData *rud;
    guint8 *data;

    if (meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
      continue;

    memset (&sei, 0, sizeof (GstH265SEIMessage));
    sei.payloadType = GST_H265_SEI_REGISTERED_USER_DATA;
    rud = &sei.payload.registered_user_data;

    rud->country_code = 181;
    rud->size = meta->size + 10;

    data = g_malloc (rud->size);
    memcpy (data + 9, meta->data, meta->size);

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
    data[7] = ((meta->size / 3) & 0x1f) | 0x40;
    data[8] = 255;              /* 8 bits em_data, unused */
    data[meta->size + 9] = 255; /* 8 marker bits */

    rud->data = data;

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
