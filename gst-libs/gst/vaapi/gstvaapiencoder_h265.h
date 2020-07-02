/*
 *  gstvaapiencoder_h265.h - H.265 encoder
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

#ifndef GST_VAAPI_ENCODER_H265_H
#define GST_VAAPI_ENCODER_H265_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiutils_h265.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_ENCODER_H265 \
    (gst_vaapi_encoder_h265_get_type ())
#define GST_VAAPI_ENCODER_H265(encoder) \
    (G_TYPE_CHECK_INSTANCE_CAST ((encoder), GST_TYPE_VAAPI_ENCODER_H265, GstVaapiEncoderH265))
#define GST_IS_VAAPI_ENCODER_H265(encoder) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((encoder), GST_TYPE_VAAPI_ENCODER_H265))

typedef struct _GstVaapiEncoderH265 GstVaapiEncoderH265;
typedef struct _GstVaapiEncoderH265Class GstVaapiEncoderH265Class;

GType
gst_vaapi_encoder_h265_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_encoder_h265_new (GstVaapiDisplay * display);

gboolean
gst_vaapi_encoder_h265_set_allowed_profiles (GstVaapiEncoderH265 * encoder,
    GArray * profiles);

gboolean
gst_vaapi_encoder_h265_get_profile_tier_level (GstVaapiEncoderH265 * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiTierH265 *out_tier_ptr, GstVaapiLevelH265 * out_level_ptr);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiEncoderH265, gst_object_unref)

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H265_H */
