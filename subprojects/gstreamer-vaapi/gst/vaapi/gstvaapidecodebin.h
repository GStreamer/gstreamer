/*
 *  gstvaapidecodebin.h
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_DECODE_BIN_H
#define GST_VAAPI_DECODE_BIN_H

#include <gst/vaapi/gstvaapifilter.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_DECODE_BIN (gst_vaapi_decode_bin_get_type ())
#define GST_VAAPI_DECODE_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_DECODE_BIN, GstVaapiDecodeBin))
#define GST_VAAPI_DECODE_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_DECODE_BIN, GstVaapiDecodeBinClass))
#define GST_IS_AUTO_DETECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_DECODE_BIN))
#define GST_IS_AUTO_DETECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_DECODE_BIN))
#define GST_VAAPI_DECODE_BIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_DECODE_BIN, GstVaapiDecodeBinClass))

typedef struct _GstVaapiDecodeBin {
  /* < private > */
  GstBin parent;

  GstElement *decoder;
  GstElement *queue;
  GstElement *postproc;

  /* properties */
  guint   max_size_buffers;
  guint   max_size_bytes;
  guint64 max_size_time;
  GstVaapiDeinterlaceMethod deinterlace_method;
  gboolean disable_vpp;

  gboolean configured;
} GstVaapiDecodeBin;

typedef struct _GstVaapiDecodeBinClass {
  GstBinClass parent_class;

} GstVaapiDecodeBinClass;

GType   gst_vaapi_decode_bin_get_type    (void);

G_END_DECLS

#endif /* GST_VAAPI_DECODE_BIN_H */
