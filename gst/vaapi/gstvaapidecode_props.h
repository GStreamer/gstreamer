/*
 *  gstvaapidecode_props.h - VA-API decoders specific properties
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

#ifndef GST_VAAPI_DECODE_PROPS_H
#define GST_VAAPI_DECODE_PROPS_H

#include "gstcompat.h"

G_BEGIN_DECLS

typedef struct _GstVaapiDecodeH264Private GstVaapiDecodeH264Private;

struct _GstVaapiDecodeH264Private
{
  gboolean is_low_latency;
  gboolean base_only;
};

void
gst_vaapi_decode_h264_install_properties (GObjectClass * klass);

GstVaapiDecodeH264Private *
gst_vaapi_decode_h264_get_instance_private (gpointer self);

G_END_DECLS

#endif /* GST_VAAPI_DECODE_PROPS_H */
