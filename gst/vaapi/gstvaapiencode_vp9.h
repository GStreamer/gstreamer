/*
 *  gstvaapiencode_vp9.h - VA-API VP9 encoder
 *
 *  Copyright (C) 2016 Intel Corporation
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

#ifndef GST_VAAPIENCODE_VP9_H
#define GST_VAAPIENCODE_VP9_H

#include <gst/gst.h>
#include "gstvaapiencode.h"

G_BEGIN_DECLS

#define GST_TYPE_VAAPIENCODE_VP9 \
    (gst_vaapiencode_vp9_get_type ())
#define GST_VAAPIENCODE_VP9_CAST(obj) \
  ((GstVaapiEncodeVP9 *)(obj))
#define GST_VAAPIENCODE_VP9(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPIENCODE_VP9, \
      GstVaapiEncodeVP9))
#define GST_VAAPIENCODE_VP9_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPIENCODE_VP9, \
      GstVaapiEncodeVP9Class))
#define GST_VAAPIENCODE_VP9_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPIENCODE_VP9, \
      GstVaapiEncodeVP9Class))
#define GST_IS_VAAPIENCODE_VP9(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPIENCODE_VP9))
#define GST_IS_VAAPIENCODE_VP9_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPIENCODE_VP9))

typedef struct _GstVaapiEncodeVP9 GstVaapiEncodeVP9;
typedef struct _GstVaapiEncodeVP9Class GstVaapiEncodeVP9Class;

struct _GstVaapiEncodeVP9
{
  /*< private >*/
  GstVaapiEncode parent_instance;
};

struct _GstVaapiEncodeVP9Class
{
  /*< private >*/
  GstVaapiEncodeClass parent_class;
};

GType
gst_vaapiencode_vp9_get_type (void) G_GNUC_CONST;

GType
gst_vaapiencode_vp9_register_type (GstVaapiDisplay * display);

G_END_DECLS

#endif /* GST_VAAPIENCODE_VP9_H */
