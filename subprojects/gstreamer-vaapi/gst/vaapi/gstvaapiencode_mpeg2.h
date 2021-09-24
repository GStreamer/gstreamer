/*
 *  gstvaapiencode_mpeg2.h - VA-API MPEG2 encoder
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Guangxin Xu <guangxin.xu@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#ifndef GST_VAAPIENCODE_MPEG2_H
#define GST_VAAPIENCODE_MPEG2_H

#include <gst/gst.h>
#include "gstvaapiencode.h"

G_BEGIN_DECLS

#define GST_TYPE_VAAPIENCODE_MPEG2 \
    (gst_vaapiencode_mpeg2_get_type ())
#define GST_VAAPIENCODE_MPEG2_CAST(obj) \
  ((GstVaapiEncodeMpeg2 *)(obj))
#define GST_VAAPIENCODE_MPEG2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPIENCODE_MPEG2, \
      GstVaapiEncodeMpeg2))
#define GST_VAAPIENCODE_MPEG2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPIENCODE_MPEG2, \
      GstVaapiEncodeMpeg2Class))
#define GST_VAAPIENCODE_MPEG2_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPIENCODE_MPEG2, \
      GstVaapiEncodeMpeg2Class))
#define GST_IS_VAAPIENCODE_MPEG2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPIENCODE_MPEG2))
#define GST_IS_VAAPIENCODE_MPEG2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPIENCODE_MPEG2))

typedef struct _GstVaapiEncodeMpeg2 GstVaapiEncodeMpeg2;
typedef struct _GstVaapiEncodeMpeg2Class GstVaapiEncodeMpeg2Class;

struct _GstVaapiEncodeMpeg2
{
  /*< private >*/
  GstVaapiEncode parent_instance;

  guint32 quantizer;
  guint32 intra_period;
  guint32 ip_period;
};

struct _GstVaapiEncodeMpeg2Class
{
  /*< private >*/
  GstVaapiEncodeClass parent_class;
};

GType
gst_vaapiencode_mpeg2_get_type (void) G_GNUC_CONST;

GType
gst_vaapiencode_mpeg2_register_type (GstVaapiDisplay * display);

G_END_DECLS

#endif /* GST_VAAPIENCODE_MPEG2_H */
