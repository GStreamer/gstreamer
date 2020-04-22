/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_NV_H264_DEC_H__
#define __GST_NV_H264_DEC_H__

#include <gst/gst.h>
#include <gst/codecs/gsth264decoder.h>

G_BEGIN_DECLS

#define GST_TYPE_NV_H264_DEC            (gst_nv_h264_dec_get_type())
#define GST_NV_H264_DEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_H264_DEC, GstNvH264Dec))
#define GST_NV_H264_DEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_NV_H264_DEC, GstNvH264DecClass))
#define GST_NV_H264_DEC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_NV_H264_DEC, GstNvH264DecClass))

typedef struct _GstNvH264Dec GstNvH264Dec;
typedef struct _GstNvH264DecClass GstNvH264DecClass;

G_GNUC_INTERNAL
GType gst_nv_h264_dec_get_type (void);

G_GNUC_INTERNAL
void gst_nv_h264_dec_register (GstPlugin * plugin,
                               guint device_id,
                               guint rank,
                               GstCaps * sink_caps,
                               GstCaps * src_caps,
                               gboolean is_primary);

G_END_DECLS

#endif /* __GST_NV_H264_DEC_H__ */
