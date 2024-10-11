/* GStreamer
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

#ifndef __GST_LCEVC_DEC_H__
#define __GST_LCEVC_DEC_H__

#include <gst/video/gstvideodecoder.h>
#include <gst/video/video-info.h>

#include <LCEVC/lcevc_dec.h>

G_BEGIN_DECLS

#define GST_TYPE_LCEVC_DEC \
  (gst_lcevc_dec_get_type())
#define GST_LCEVC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LCEVC_DEC,GstLcevcDec))
#define GST_LCEVC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LCEVC_DEC,GstLcevcDecClass))
#define GST_IS_LCEVC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LCEVC_DEC))
#define GST_IS_LCEVC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LCEVC_DEC))

typedef struct _GstLcevcDec GstLcevcDec;
typedef struct _GstLcevcDecClass GstLcevcDecClass;

struct _GstLcevcDec {
  GstVideoDecoder video_decoder;

  /* Props */
  gboolean verbose;
  gint max_width;
  gint max_height;
  gint max_latency;

  LCEVC_DecoderHandle decoder_handle;
  GstVideoInfo in_info;
  gboolean can_crop;

  guint32 out_width;
  guint32 out_height;
  guint32 out_crop_top;
  guint32 out_crop_bottom;
  guint32 out_crop_left;
  guint32 out_crop_right;
};

struct _GstLcevcDecClass {
  GstVideoDecoderClass video_decoder_class;
};

GType gst_lcevc_dec_get_type(void);

GST_ELEMENT_REGISTER_DECLARE (lcevcdec);

G_END_DECLS

#endif /* __GST_LCEVC_DEC_H__ */
