/*
 *  gstvaapidecode_props.c - VA-API decoders specific properties
 *
 *  Copyright (C) 2017 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *    Author: Victor Jaquez <vjaquez@igalia.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapidecode_props.h"

static void
gst_vaapi_decode_h264_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
}

static void
gst_vaapi_decode_h264_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
}

void
gst_vaapi_decode_h264_install_properties (GObjectClass * klass)
{
  klass->get_property = gst_vaapi_decode_h264_get_property;
  klass->set_property = gst_vaapi_decode_h264_set_property;
}
