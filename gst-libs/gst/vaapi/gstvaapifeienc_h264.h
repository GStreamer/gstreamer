/*
 *  gstvaapifeienc_h264.h - FEI Enc abstract
 *
 *  Copyright (C) 2016-2018 Intel Corporation
 *    Author: Leilei Shang <leilei.shang@intel.com>
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

#ifndef GST_VAAPI_FEI_H264_ENC_H
#define GST_VAAPI_FEI_H264_ENC_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiutils_h264.h>
#include <gst/vaapi/gstvaapifeiutils_h264.h>
#include <gst/vaapi/gstvaapifei_objects_priv.h>
G_BEGIN_DECLS

#define GST_TYPE_VAAPI_FEI_ENC_H264 \
    (gst_vaapi_feienc_h264_get_type ())
#define GST_VAAPI_FEI_ENC_H264(encoder) \
    (G_TYPE_CHECK_INSTANCE_CAST ((encoder), GST_TYPE_VAAPI_FEI_ENC_H264, GstVaapiFeiEncH264))
#define GST_IS_VAAPI_FEI_ENC_H264(encoder) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((encoder), GST_TYPE_VAAPI_FEI_ENC_H264))

typedef struct _GstVaapiFeiEncH264 GstVaapiFeiEncH264;
typedef struct _GstVaapiFeiEncH264Class GstVaapiFeiEncH264Class;

GType
gst_vaapi_feienc_h264_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_feienc_h264_new (GstVaapiDisplay * display);

gboolean
gst_vaapi_feienc_h264_set_max_profile (GstVaapiFeiEncH264 * feienc,
    GstVaapiProfile profile);

gboolean
gst_vaapi_feienc_h264_set_ref_pool (GstVaapiFeiEncH264 * feienc, gpointer ref_pool_ptr);

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * reconstruct,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiFeiInfoToPakH264 *info_to_pak);

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_flush (GstVaapiEncoder * base_encoder);

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output);

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_reconfigure (GstVaapiEncoder * base_encoder);

gboolean
gst_vaapi_feienc_h264_get_profile_and_idc (GstVaapiFeiEncH264 * feienc,
    GstVaapiProfile * out_profile_ptr, guint8 * out_profile_idc_ptr);

G_END_DECLS
#endif /*GST_VAAPI_FEI_H264_ENC_H */
