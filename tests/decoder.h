/*
 *  decoder.h - Decoder utilities for the tests
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef DECODER_H
#define DECODER_H

#include <gst/vaapi/gstvaapidecoder.h>

GstVaapiDecoder *
decoder_new(GstVaapiDisplay *display, const gchar *codec_name);

gboolean
decoder_put_buffers(GstVaapiDecoder *decoder);

GstVaapiSurfaceProxy *
decoder_get_surface(GstVaapiDecoder *decoder);

const gchar *
decoder_get_codec_name(GstVaapiDecoder *decoder);

#endif /* DECODER_H */
