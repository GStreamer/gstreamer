/*
 *  gstvaapidecode.h - VA-API video decoder
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#include <gst/gst.h>
#include <gst/gsttask.h>
#include <gst/video/gstvideodecoder.h>
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
    GstVideoDecoder     parent_instance;

    GstPad             *sinkpad;
    GstCaps            *sinkpad_caps;
    GstPadQueryFunction sinkpad_query;
    GstPad             *srcpad;
    GstCaps            *srcpad_caps;
    GstPadQueryFunction srcpad_query;
    GstVaapiDisplay    *display;
    GstVaapiDecoder    *decoder;
    GMutex              decoder_mutex;
    GCond               decoder_ready;
    GstCaps            *decoder_caps;
    GstCaps            *allowed_caps;
    gint64              render_time_base;
    GstClockTime        last_buffer_time;
};

struct _GstVaapiDecodeClass {
    /*< private >*/
    GstVideoDecoderClass parent_class;
};

GType
gst_vaapidecode_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPIDECODE_H */
