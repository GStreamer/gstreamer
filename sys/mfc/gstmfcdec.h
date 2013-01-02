/* 
 * Copyright (C) 2012 Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 *
 */

#ifndef __GST_MFC_DEC_H__
#define __GST_MFC_DEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "mfc_decoder/mfc_decoder.h"
#include "fimc/fimc.h"

G_BEGIN_DECLS

#define GST_TYPE_MFC_DEC \
  (gst_mfc_dec_get_type())
#define GST_MFC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MFC_DEC,GstMFCDec))
#define GST_MFC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MFC_DEC,GstMFCDecClass))
#define GST_IS_MFC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MFC_DEC))
#define GST_IS_MFC_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MFC_DEC))

typedef struct _GstMFCDec GstMFCDec;
typedef struct _GstMFCDecClass GstMFCDecClass;

struct _GstMFCDec
{
  GstVideoDecoder parent;

  /* < private > */
  GstVideoCodecState *input_state;
  struct mfc_dec_context* context;
  gboolean initialized;
  GstBuffer *codec_data;

  gboolean has_cropping;

  GstVideoFormat format;  
  FimcColorFormat fimc_format;
  Fimc *fimc;
  gint width, height;
  gint crop_left, crop_top;
  gint crop_width, crop_height;
  int src_stride[3];

  void *dst[3];
  int dst_stride[3];
  gboolean mmap;
};

struct _GstMFCDecClass
{
  GstVideoDecoderClass parent_class;
};

GType gst_mfc_dec_get_type (void);

G_END_DECLS

#endif /* __GST_MFC_DEC_H__ */
