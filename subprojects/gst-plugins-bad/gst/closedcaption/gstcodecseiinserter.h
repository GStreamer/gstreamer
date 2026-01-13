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

#define GST_TYPE_CODEC_SEI_INSERTER             (gst_codec_sei_inserter_get_type())
#define GST_CODEC_SEI_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CODEC_SEI_INSERTER,GstCodecSEIInserter))
#define GST_CODEC_SEI_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CODEC_SEI_INSERTER,GstCodecSEIInserterClass))
#define GST_IS_CODEC_SEI_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CODEC_SEI_INSERTER))
#define GST_IS_CODEC_SEI_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CODEC_SEI_INSERTER))
#define GST_CODEC_SEI_INSERTER_GET_CLASS(obj)   (GST_CODEC_SEI_INSERTER_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_CODEC_SEI_INSERTER_CAST(obj)        ((GstCodecSEIInserter*)(obj))

typedef struct _GstCodecSEIInserter GstCodecSEIInserter;
typedef struct _GstCodecSEIInserterClass GstCodecSEIInserterClass;
typedef struct _GstCodecSEIInserterPrivate GstCodecSEIInserterPrivate;

typedef enum
{
  GST_CODEC_SEI_INSERT_META_ORDER_DECODE,
  GST_CODEC_SEI_INSERT_META_ORDER_DISPLAY,
} GstCodecSEIInsertMetaOrder;

/**
 * GstCodecSEIInsertType:
 * @GST_CODEC_SEI_INSERT_CC: Insert closed caption SEI messages
 * @GST_CODEC_SEI_INSERT_UNREGISTERED: Insert unregistered user data SEI messages
 *
 * Flags to control which SEI message types to insert.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_CODEC_SEI_INSERT_CC = (1 << 0),
  GST_CODEC_SEI_INSERT_UNREGISTERED = (1 << 1),
} GstCodecSEIInsertType;

#define GST_CODEC_SEI_INSERT_ALL (GST_CODEC_SEI_INSERT_CC | GST_CODEC_SEI_INSERT_UNREGISTERED)

struct _GstCodecSEIInserter
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstCodecSEIInserterPrivate *priv;
};

struct _GstCodecSEIInserterClass
{
  GstElementClass parent_class;

  gboolean      (*start)         (GstCodecSEIInserter * inserter,
                                  GstCodecSEIInsertMetaOrder meta_order);

  gboolean      (*stop)          (GstCodecSEIInserter * inserter);

  gboolean      (*set_caps)      (GstCodecSEIInserter * inserter,
                                  GstCaps * caps,
                                  GstClockTime * latency);

  guint         (*get_num_buffered) (GstCodecSEIInserter * inserter);

  gboolean      (*push)          (GstCodecSEIInserter * inserter,
                                  GstVideoCodecFrame * frame,
                                  GstClockTime * latency);

  GstVideoCodecFrame * (*pop)    (GstCodecSEIInserter * inserter);

  void          (*drain)         (GstCodecSEIInserter * inserter);

  GstBuffer *   (*insert_sei)    (GstCodecSEIInserter * inserter,
                                  GstBuffer * buffer,
                                  GPtrArray * metas);
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstCodecSEIInserter, gst_object_unref);

GType gst_codec_sei_inserter_get_type (void);

#define GST_TYPE_CODEC_SEI_INSERT_TYPE (gst_codec_sei_insert_type_get_type())
GType gst_codec_sei_insert_type_get_type (void);

/* Property IDs for subclasses */
enum
{
  GST_CODEC_SEI_INSERTER_PROP_0,
  GST_CODEC_SEI_INSERTER_PROP_CAPTION_META_ORDER,
  GST_CODEC_SEI_INSERTER_PROP_REMOVE_CAPTION_META,
  GST_CODEC_SEI_INSERTER_PROP_SEI_TYPES,
  GST_CODEC_SEI_INSERTER_PROP_REMOVE_SEI_UNREGISTERED_META,
};

void gst_codec_sei_inserter_set_sei_types (GstCodecSEIInserter * inserter,
    GstCodecSEIInsertType sei_types);
GstCodecSEIInsertType gst_codec_sei_inserter_get_sei_types (GstCodecSEIInserter * inserter);

void gst_codec_sei_inserter_set_remove_sei_unregistered_meta (GstCodecSEIInserter * inserter,
    gboolean remove);
gboolean gst_codec_sei_inserter_get_remove_sei_unregistered_meta (GstCodecSEIInserter * inserter);

G_END_DECLS
