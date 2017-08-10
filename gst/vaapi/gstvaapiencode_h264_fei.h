/*
 *  gstvaapiencode_h264i_fei.h - VA-API H.264 FEI encoder
 *
 *  Copyright (C) 2016-2017 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *    Author: Yi A Wang <yi.a.wang@intel.com>
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

#ifndef GST_VAAPIENCODE_H264_FEI_FEI_H
#define GST_VAAPIENCODE_H264_FEI_FEI_H

#include <gst/gst.h>
#include "gstvaapiencode.h"

G_BEGIN_DECLS

#define GST_TYPE_VAAPIENCODE_H264_FEI \
    (gst_vaapiencode_h264_fei_get_type ())
#define GST_VAAPIENCODE_H264_FEI_CAST(obj) \
  ((GstVaapiEncodeH264Fei *)(obj))
#define GST_VAAPIENCODE_H264_FEI(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPIENCODE_H264_FEI, \
      GstVaapiEncodeH264Fei))
#define GST_VAAPIENCODE_H264_FEI_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPIENCODE_H264_FEI, \
      GstVaapiEncodeH264FeiClass))
#define GST_VAAPIENCODE_H264_FEI_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPIENCODE_H264_FEI, \
      GstVaapiEncodeH264FeiClass))
#define GST_IS_VAAPIENCODE_H264_FEI(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPIENCODE_H264_FEI))
#define GST_IS_VAAPIENCODE_H264_FEI_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPIENCODE_H264_FEI))

typedef struct _GstVaapiEncodeH264Fei GstVaapiEncodeH264Fei;
typedef struct _GstVaapiEncodeH264FeiClass GstVaapiEncodeH264FeiClass;

struct _GstVaapiEncodeH264Fei
{
  /*< private >*/
  GstVaapiEncode parent_instance;

  guint is_avc:1; /* [FALSE]=byte-stream (default); [TRUE]=avcC */
};

struct _GstVaapiEncodeH264FeiClass
{
  /*< private >*/
  GstVaapiEncodeClass parent_class;
};

GType
gst_vaapiencode_h264_fei_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPIENCODE_H264_FEI_FEI_H */
