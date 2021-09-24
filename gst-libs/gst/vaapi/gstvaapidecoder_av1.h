/*
 *  gstvaapidecoder_av1.h - AV1 decoder
 *
 *  Copyright (C) 2019-2020 Intel Corporation
 *    Author: Junyan He <junyan.he@hotmail.com>
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

#ifndef GST_VAAPI_DECODER_AV1_H
#define GST_VAAPI_DECODER_AV1_H

#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_DECODER_AV1 \
    (gst_vaapi_decoder_av1_get_type ())
#define GST_VAAPI_DECODER_AV1(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_DECODER_AV1, GstVaapiDecoderAV1))
#define GST_VAAPI_IS_DECODER_AV1(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_DECODER_AV1))

typedef struct _GstVaapiDecoderAV1              GstVaapiDecoderAV1;

GType
gst_vaapi_decoder_av1_get_type (void) G_GNUC_CONST;

GstVaapiDecoder *
gst_vaapi_decoder_av1_new (GstVaapiDisplay * display, GstCaps * caps);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_AV1_H */
