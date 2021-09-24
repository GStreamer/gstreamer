/*
 *  gstvaapiencode_vp8.h - VA-API VP8 encoder
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

#ifndef GST_VAAPIENCODE_VP8_H
#define GST_VAAPIENCODE_VP8_H

#include <gst/gst.h>
#include "gstvaapiencode.h"

G_BEGIN_DECLS

#define GST_TYPE_VAAPIENCODE_VP8 \
    (gst_vaapiencode_vp8_get_type ())
#define GST_VAAPIENCODE_VP8_CAST(obj) \
  ((GstVaapiEncodeVP8 *)(obj))
#define GST_VAAPIENCODE_VP8(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPIENCODE_VP8, \
      GstVaapiEncodeVP8))
#define GST_VAAPIENCODE_VP8_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPIENCODE_VP8, \
      GstVaapiEncodeVP8Class))
#define GST_VAAPIENCODE_VP8_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPIENCODE_VP8, \
      GstVaapiEncodeVP8Class))
#define GST_IS_VAAPIENCODE_VP8(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPIENCODE_VP8))
#define GST_IS_VAAPIENCODE_VP8_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPIENCODE_VP8))

typedef struct _GstVaapiEncodeVP8 GstVaapiEncodeVP8;
typedef struct _GstVaapiEncodeVP8Class GstVaapiEncodeVP8Class;

struct _GstVaapiEncodeVP8
{
  /*< private >*/
  GstVaapiEncode parent_instance;
};

struct _GstVaapiEncodeVP8Class
{
  /*< private >*/
  GstVaapiEncodeClass parent_class;
};

GType
gst_vaapiencode_vp8_get_type (void) G_GNUC_CONST;

GType
gst_vaapiencode_vp8_register_type (GstVaapiDisplay * display);

G_END_DECLS

#endif /* GST_VAAPIENCODE_VP8_H */
