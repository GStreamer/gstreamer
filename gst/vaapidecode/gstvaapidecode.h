/*
 *  gstvaapidecode.h - VA-API video decoder
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011 Intel Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef GST_VAAPIDECODE_H
#define GST_VAAPIDECODE_H

#include <gst/gst.h>
#include <gst/gsttask.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIDECODE \
    (gst_vaapidecode_get_type())

#define GST_VAAPIDECODE(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_TYPE_VAAPIDECODE,   \
                                GstVaapiDecode))

#define GST_VAAPIDECODE_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_TYPE_VAAPIDECODE,      \
                             GstVaapiDecodeClass))

#define GST_IS_VAAPIDECODE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPIDECODE))

#define GST_IS_VAAPIDECODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPIDECODE))

#define GST_VAAPIDECODE_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_TYPE_VAAPIDECODE,    \
                               GstVaapiDecodeClass))

typedef struct _GstVaapiDecode                  GstVaapiDecode;
typedef struct _GstVaapiDecodeClass             GstVaapiDecodeClass;

struct _GstVaapiDecode {
    /*< private >*/
    GstElement          parent_instance;

    GstPad             *sinkpad;
    GstCaps            *sinkpad_caps;
    GstPad             *srcpad;
    GstCaps            *srcpad_caps;
    GstVaapiDisplay    *display;
    GstVaapiDecoder    *decoder;
    GMutex             *decoder_mutex;
    GCond              *decoder_ready;
    GstCaps            *decoder_caps;
    GstCaps            *allowed_caps;
    unsigned int        use_ffmpeg      : 1;
};

struct _GstVaapiDecodeClass {
    /*< private >*/
    GstElementClass     parent_class;
};

GType
gst_vaapidecode_get_type(void);

G_END_DECLS

#endif /* GST_VAAPIDECODE_H */
