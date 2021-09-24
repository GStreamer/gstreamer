/*
 *  gstvaapidecode.h - VA-API video decoder
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
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

#ifndef GST_VAAPIDECODE_H
#define GST_VAAPIDECODE_H

#include "gstvaapipluginbase.h"
#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

#define GST_VAAPIDECODE(obj) ((GstVaapiDecode *)(obj))

typedef struct _GstVaapiDecode                  GstVaapiDecode;
typedef struct _GstVaapiDecodeClass             GstVaapiDecodeClass;

struct _GstVaapiDecode {
    /*< private >*/
    GstVaapiPluginBase  parent_instance;

    GstCaps            *sinkpad_caps;
    GstCaps            *srcpad_caps;
    GstVideoInfo        decoded_info;
    GstVaapiDecoder    *decoder;
    GstCaps            *allowed_sinkpad_caps;
    GstCaps            *allowed_srcpad_caps;
    guint               current_frame_size;
    guint               has_texture_upload_meta : 1;

    guint               display_width;
    guint               display_height;

    GstVideoCodecState *input_state;

    gboolean            do_renego;
};

struct _GstVaapiDecodeClass {
    /*< private >*/
    GstVaapiPluginBaseClass parent_class;
};

gboolean gst_vaapidecode_register (GstPlugin * plugin, GArray * decoders);

G_END_DECLS

#endif /* GST_VAAPIDECODE_H */
