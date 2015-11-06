/*
 *  gstvaapidecoder_vp9.h - VP9 decoder
 *
 *  Copyright (C) 2015-2016 Intel Corporation
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

#ifndef GST_VAAPI_DECODER_VP9_H
#define GST_VAAPI_DECODER_VP9_H

#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

typedef struct _GstVaapiDecoderVp9              GstVaapiDecoderVp9;

GstVaapiDecoder *
gst_vaapi_decoder_vp9_new (GstVaapiDisplay * display, GstCaps * caps);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_VP9_H */
