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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_CODEC_CC_INSERTER             (gst_codec_cc_inserter_get_type())
#define GST_CODEC_CC_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CODEC_CC_INSERTER,GstCodecCCInserter))
#define GST_CODEC_CC_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CODEC_CC_INSERTER,GstCodecCCInserterClass))
#define GST_CODEC_CC_INSERTER_GET_CLASS(obj)   (GST_CODEC_CC_INSERTER_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_CODEC_CC_INSERTER_CAST(obj)        ((GstCodecCCInserter*)(obj))

typedef struct _GstCodecCCInserter GstCodecCCInserter;
typedef struct _GstCodecCCInserterClass GstCodecCCInserterClass;
typedef struct _GstCodecCCInserterPrivate GstCodecCCInserterPrivate;

typedef enum
{
  GST_CODEC_CC_INSERT_META_ORDER_DECODE,
  GST_CODEC_CC_INSERT_META_ORDER_DISPLAY,
} GstCodecCCInsertMetaOrder;

struct _GstCodecCCInserter
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstCodecCCInserterPrivate *priv;
};

struct _GstCodecCCInserterClass
{
  GstElementClass parent_class;

  gboolean      (*start)         (GstCodecCCInserter * inserter,
                                  GstCodecCCInsertMetaOrder meta_order);

  gboolean      (*stop)          (GstCodecCCInserter * inserter);

  gboolean      (*set_caps)      (GstCodecCCInserter * inserter,
                                  GstCaps * caps,
                                  GstClockTime * latency);

  guint         (*get_num_buffered) (GstCodecCCInserter * inserter);

  gboolean      (*push)          (GstCodecCCInserter * inserter,
                                  GstVideoCodecFrame * frame,
                                  GstClockTime * latency);

  GstVideoCodecFrame * (*pop)    (GstCodecCCInserter * inserter);

  void          (*drain)         (GstCodecCCInserter * inserter);

  GstBuffer *   (*insert_cc)     (GstCodecCCInserter * inserter,
                                  GstBuffer * buffer,
                                  GPtrArray * metas);
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstCodecCCInserter, gst_object_unref);

GType gst_codec_cc_inserter_get_type (void);

G_END_DECLS
