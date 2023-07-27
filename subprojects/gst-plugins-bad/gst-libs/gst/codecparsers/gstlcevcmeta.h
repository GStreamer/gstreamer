/* GStreamer LCEVC meta
 *  Copyright (C) <2024> V-Nova International Limited
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

#ifndef __GST_LCEVC_META_H__
#define __GST_LCEVC_META_H__

#include <gst/gst.h>
#include <gst/codecparsers/codecparsers-prelude.h>

G_BEGIN_DECLS

#define GST_LCEVC_META_API_TYPE (gst_lcevc_meta_api_get_type())
#define GST_LCEVC_META_INFO  (gst_lcevc_meta_get_info())
typedef struct _GstLcevcMeta GstLcevcMeta;

#define GST_CAPS_FEATURE_META_GST_LCEVC_META "meta:GstLcevcMeta"

/**
 * GstLcevcMeta:
 * @meta: parent #GstMeta
 * @id: the id of the LCEVC meta
 * @enhancement_data: the parsed LCEVC enhancement data
 *
 * LCEVC data for LCEVC codecs
 *
 * Since: 1.26
 */
struct _GstLcevcMeta {
  GstMeta meta;

  gint id;
  GstBuffer *enhancement_data;
};

GST_CODEC_PARSERS_API
GType gst_lcevc_meta_api_get_type (void);

GST_CODEC_PARSERS_API
const GstMetaInfo * gst_lcevc_meta_get_info (void);

GST_CODEC_PARSERS_API
GstLcevcMeta * gst_buffer_get_lcevc_meta (GstBuffer *buffer);

GST_CODEC_PARSERS_API
GstLcevcMeta * gst_buffer_get_lcevc_meta_id (GstBuffer *buffer, gint id);

GST_CODEC_PARSERS_API
GstLcevcMeta * gst_buffer_add_lcevc_meta (GstBuffer *buffer,
    GstBuffer *enhancement_data);

G_END_DECLS

#endif /* __GST_VIDEO_META_H__ */
