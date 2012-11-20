/*
 *  gstvaapiuploader.h - VA-API video upload helper
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPIUPLOADER_H
#define GST_VAAPIUPLOADER_H

#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_UPLOADER \
    (gst_vaapi_uploader_get_type())

#define GST_VAAPI_UPLOADER(obj)                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_UPLOADER,        \
                                GstVaapiUploader))

#define GST_VAAPI_UPLOADER_CLASS(klass)                         \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_UPLOADER,           \
                             GstVaapiUploaderClass))

#define GST_VAAPI_IS_UPLOADER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_UPLOADER))

#define GST_VAAPI_IS_UPLOADER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_UPLOADER))

#define GST_VAAPI_UPLOADER_GET_CLASS(obj)                       \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_UPLOADER,         \
                               GstVaapiUploaderClass))

typedef struct _GstVaapiUploader                GstVaapiUploader;
typedef struct _GstVaapiUploaderPrivate         GstVaapiUploaderPrivate;
typedef struct _GstVaapiUploaderClass           GstVaapiUploaderClass;

struct _GstVaapiUploader {
    /*< private >*/
    GObject             parent_instance;

    GstVaapiUploaderPrivate *priv;
};

struct _GstVaapiUploaderClass {
    /*< private >*/
    GObjectClass        parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_uploader_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiUploader *
gst_vaapi_uploader_new(GstVaapiDisplay *display);

G_GNUC_INTERNAL
gboolean
gst_vaapi_uploader_ensure_display(
    GstVaapiUploader *uploader,
    GstVaapiDisplay  *display
);

G_GNUC_INTERNAL
gboolean
gst_vaapi_uploader_ensure_caps(
    GstVaapiUploader *uploader,
    GstCaps          *src_caps,
    GstCaps          *out_caps
);

G_GNUC_INTERNAL
gboolean
gst_vaapi_uploader_process(
    GstVaapiUploader *uploader,
    GstBuffer        *src_buffer,
    GstBuffer        *out_buffer
);

G_GNUC_INTERNAL
GstCaps *
gst_vaapi_uploader_get_caps(GstVaapiUploader *uploader);

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_uploader_get_buffer(GstVaapiUploader *uploader);

G_GNUC_INTERNAL
gboolean
gst_vaapi_uploader_has_direct_rendering(GstVaapiUploader *uploader);

G_END_DECLS

#endif /* GST_VAAPI_UPLOADER_H */
