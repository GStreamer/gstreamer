/*
 *  codec.h - Codec utilities for the tests
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

#ifndef CODEC_H
#define CODEC_H

#include <gst/vaapi/gstvaapiprofile.h>

const gchar *
string_from_codec(GstVaapiCodec codec);

GstCaps *
caps_from_codec(GstVaapiCodec codec);

GstVaapiCodec
identify_codec_from_string(const gchar *codec_str);

GstVaapiCodec
identify_codec(const gchar *filename);

#endif /* CODEC_H */
