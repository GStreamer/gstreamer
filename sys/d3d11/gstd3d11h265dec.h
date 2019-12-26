/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_H265_DEC_H__
#define __GST_D3D11_H265_DEC_H__

#include "gsth265decoder.h"
#include "gsth265picture.h"
#include "gstd3d11decoder.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_H265_DEC \
  (gst_d3d11_h265_dec_get_type())
#define GST_D3D11_H265_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_H265_DEC,GstD3D11H265Dec))
#define GST_D3D11_H265_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D11_H265_DEC,GstD3D11H265DecClass))
#define GST_D3D11_H265_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_D3D11_H265_DEC,GstD3D11H265DecClass))
#define GST_IS_D3D11_H265_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_H265_DEC))
#define GST_IS_D3D11_H265_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D11_H265_DEC))

struct _GstD3D11H265Dec
{
  GstH265Decoder parent;

  GstVideoCodecState *output_state;

  GstD3D11Device *device;
  gint adapter;

  guint width, height;
  guint coded_width, coded_height;
  guint bitdepth;
  guint chroma_format_idc;
  GstVideoFormat out_format;

  DXVA_PicEntry_HEVC ref_pic_list[15];
  INT pic_order_cnt_val_list[15];
  UCHAR ref_pic_set_st_curr_before[8];
  UCHAR ref_pic_set_st_curr_after[8];
  UCHAR ref_pic_set_lt_curr[8];

  /* Array of DXVA_Slice_HEVC_Short */
  GArray *slice_list;
  gboolean submit_iq_data;

  GstD3D11Decoder *d3d11_decoder;

  GstH265Picture *current_picture;

  /* Pointing current bitstream buffer */
  guint current_offset;
  guint bitstream_buffer_size;
  guint8 * bitstream_buffer_bytes;

  gboolean use_d3d11_output;
};

struct _GstD3D11H265DecClass
{
  GstH265DecoderClass parent_class;
};

GType gst_d3d11_h265_dec_get_type (void);

G_END_DECLS

#endif /* __GST_D3D11_H265_DEC_H__ */
