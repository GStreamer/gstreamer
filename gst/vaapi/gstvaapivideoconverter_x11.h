/*
 *  gstvaapivideoconverter_x11.h - VA video converter to X11 pixmap
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

#ifndef GST_VAAPI_VIDEO_CONVERTER_X11_H
#define GST_VAAPI_VIDEO_CONVERTER_X11_H

#include <gst/video/gstsurfaceconverter.h>
#include "gstvaapivideobuffer.h"

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_CONVERTER_X11 \
  (gst_vaapi_video_converter_x11_get_type ())
#define GST_VAAPI_VIDEO_CONVERTER_X11(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_VAAPI_TYPE_VIDEO_CONVERTER_X11, \
      GstVaapiVideoConverterX11))
#define GST_VAAPI_VIDEO_CONVERTER_X11_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_VAAPI_TYPE_VIDEO_CONVERTER_X11, \
      GstVaapiVideoConverterX11Class))
#define GST_VAAPI_IS_VIDEO_CONVERTER_X11(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_VAAPI_TYPE_VIDEO_CONVERTER_X11))
#define GST_VAAPI_IS_VIDEO_CONVERTER_X11_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_VAAPI_TYPE_VIDEO_CONVERTER_X11))
#define GST_VAAPI_VIDEO_CONVERTER_X11_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_VAAPI_TYPE_VIDEO_CONVERTER_X11, \
      GstVaapiVideoConverterX11Class))

typedef struct _GstVaapiVideoConverterX11 GstVaapiVideoConverterX11;
typedef struct _GstVaapiVideoConverterX11Private
    GstVaapiVideoConverterX11Private;
typedef struct _GstVaapiVideoConverterX11Class GstVaapiVideoConverterX11Class;

/**
 * GstVaapiVideoConverterX11:
 *
 * Converter to transform VA buffers into GL textures.
 */
struct _GstVaapiVideoConverterX11
{
  /*< private >*/
  GObject parent_instance;

  GstVaapiVideoConverterX11Private *priv;
};

/**
 * GstVaapiVideoConverterX11Class:
 *
 * Converter class to transform VA buffers into GL textures.
 */
struct _GstVaapiVideoConverterX11Class
{
  /*< private >*/
  GObjectClass parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_video_converter_x11_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstSurfaceConverter *
gst_vaapi_video_converter_x11_new (GstBuffer * buffer, const gchar * type,
    GValue * dest);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_CONVERTER_X11_H */
