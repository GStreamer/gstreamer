/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_BASE_METADATA_H__
#define __GST_BASE_METADATA_H__

#include <gst/gst.h>
#include "metadata.h"

G_BEGIN_DECLS

/* *INDENT-OFF* */
#define GST_TYPE_BASE_METADATA            (gst_base_metadata_get_type())
#define GST_BASE_METADATA(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
  GST_TYPE_BASE_METADATA,GstBaseMetadata))
#define GST_BASE_METADATA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
  GST_TYPE_BASE_METADATA,GstBaseMetadataClass))
#define GST_BASE_METADATA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
  GST_TYPE_BASE_METADATA, GstBaseMetadataClass))
#define GST_IS_BASE_METADATA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
  GST_TYPE_BASE_METADATA))
#define GST_IS_BASE_METADATA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
  GST_TYPE_BASE_METADATA))
#define GST_BASE_METADATA_CAST(obj)       ((GstBaseMetadata *)(obj))
/* *INDENT-ON* */

typedef struct _GstBaseMetadata GstBaseMetadata;
typedef struct _GstBaseMetadataClass GstBaseMetadataClass;

enum {
  BASE_METADATA_DEMUXING,
  BASE_METADATA_MUXING
};


/*
 * GST_BASE_METADATA_SRC_PAD:
 * @obj: base metadata instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define GST_BASE_METADATA_SRC_PAD(obj) (GST_BASE_METADATA_CAST (obj)->srcpad)

/*
 * GST_BASE_METADATA_SINK_PAD:
 * @obj: base metadata instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define GST_BASE_METADATA_SINK_PAD(obj) (GST_BASE_METADATA_CAST (obj)->sinkpad)

/*
 * GST_BASE_METADATA_EXIF_ADAPTER
 * @obj: base metadata instance
 *
 * Gives the pointer to the EXIF #GstAdapter of the element.
 */
#define GST_BASE_METADATA_EXIF_ADAPTER(obj) \
    (GST_BASE_METADATA_CAST (obj)->metadata->exif_adapter)

/*
 * GST_BASE_METADATA_IPTC_ADAPTER
 * @obj: base metadata instance
 *
 * Gives the pointer to the IPTC #GstAdapter of the element.
 */
#define GST_BASE_METADATA_IPTC_ADAPTER(obj) \
    (GST_BASE_METADATA_CAST (obj)->metadata->iptc_adapter)

/*
 * GST_BASE_METADATA_XMP_ADAPTER
 * @obj: base metadata instance
 *
 * Gives the pointer to the XMP #GstAdapter of the element.
 */
#define GST_BASE_METADATA_XMP_ADAPTER(obj) \
    (GST_BASE_METADATA_CAST (obj)->metadata->xmp_adapter)

/*
 * GST_BASE_METADATA_IMG_TYPE
 * @obj: base metadata instance
 *
 * Gives the type indentified by the parser of the element.
 */
#define GST_BASE_METADATA_IMG_TYPE(obj) \
    (GST_BASE_METADATA_CAST (obj)->img_type)


typedef enum _MetadataState
{
  MT_STATE_NULL,                /* still need to check media type */
  MT_STATE_PARSED
} MetadataState;

/**
 * GstBaseMetadata:
 *
 * The opaque #GstBaseMetadata data structure.
 */
struct _GstBaseMetadata
{
  GstElement element;

  /*< protected >*/
  GstPad *sinkpad, *srcpad;

  MetaData *metadata; /* handle for parsing module */

  ImageType img_type;

  /*< private >*/

  gint64 duration_orig;     /* durarion of stream */
  gint64 duration;          /* durarion of modified stream */

  MetadataState state;

  MetaOptions options;

  gboolean need_processing; /* still need a action before send first buffer */

  GstAdapter *adapter_parsing;
  GstAdapter *adapter_holding;
  guint32 next_offset;
  guint32 next_size;
  gboolean need_more_data;
  gint64 offset_orig;  /* offset in original stream */
  gint64 offset;       /* offset in current stream */

  GstBuffer * append_buffer;
  GstBuffer * prepend_buffer;

};

struct _GstBaseMetadataClass
{
  GstElementClass parent_class;

  void (*processing) (GstBaseMetadata *basemetadata);

  gboolean (*set_caps) (GstPad * pad, GstCaps * caps);

  GstCaps* (*get_src_caps) (GstPad * pad);
  GstCaps* (*get_sink_caps) (GstPad * pad);

  gboolean (*sink_event) (GstPad * pad, GstEvent * event);

};

extern GType
gst_base_metadata_get_type (void);

extern void
gst_base_metadata_set_option_flag(GstBaseMetadata *base,
    const MetaOptions options);

extern void
gst_base_metadata_unset_option_flag(GstBaseMetadata *base,
    const MetaOptions options);

extern MetaOptions
gst_base_metadata_get_option_flag(const GstBaseMetadata *base);

extern void
gst_base_metadata_update_inject_segment_with_new_data (GstBaseMetadata *base,
    guint8 ** data, guint32 * size, MetadataChunkType type);

G_END_DECLS
#endif /* __GST_BASE_METADATA_H__ */
