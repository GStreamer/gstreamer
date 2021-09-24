/*
 *  gstvaapiencoder_vp9.h VP9 encoder
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

#ifndef GST_VAAPI_ENCODER_VP9_H
#define GST_VAAPI_ENCODER_VP9_H

#include <gst/vaapi/gstvaapiencoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_ENCODER_VP9 \
    (gst_vaapi_encoder_vp9_get_type ())
#define GST_VAAPI_ENCODER_VP9(encoder) \
    (G_TYPE_CHECK_INSTANCE_CAST ((encoder), GST_TYPE_VAAPI_ENCODER_VP9, GstVaapiEncoderVP9))
#define GST_IS_VAAPI_ENCODER_VP9(encoder) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((encoder), GST_TYPE_VAAPI_ENCODER_VP9))

typedef struct _GstVaapiEncoderVP9 GstVaapiEncoderVP9;
typedef struct _GstVaapiEncoderVP9Class GstVaapiEncoderVP9Class;

GType
gst_vaapi_encoder_vp9_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_encoder_vp9_new (GstVaapiDisplay * display);

gboolean
gst_vaapi_encoder_vp9_set_allowed_profiles (GstVaapiEncoderVP9 * encoder,
    GArray * profiles);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiEncoderVP9, gst_object_unref)

G_END_DECLS
#endif /*GST_VAAPI_ENCODER_VP9_H */
