/*
 *  gstvaapidownload.h - VA-API video downloader
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

#ifndef GST_VAAPIDOWNLOAD_H
#define GST_VAAPIDOWNLOAD_H

#include <gst/base/gstbasetransform.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimagepool.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapivideobuffer.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIDOWNLOAD \
    (gst_vaapidownload_get_type())

#define GST_VAAPIDOWNLOAD(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_TYPE_VAAPIDOWNLOAD, \
                                GstVaapiDownload))

#define GST_VAAPIDOWNLOAD_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_TYPE_VAAPIDOWNLOAD,    \
                             GstVaapiDownloadClass))

#define GST_IS_VAAPIDOWNLOAD(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPIDOWNLOAD))

#define GST_IS_VAAPIDOWNLOAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPIDOWNLOAD))

#define GST_VAAPIDOWNLOAD_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_TYPE_VAAPIDOWNLOAD,  \
                               GstVaapiDownloadClass))

typedef struct _GstVaapiDownload                GstVaapiDownload;
typedef struct _GstVaapiDownloadClass           GstVaapiDownloadClass;

GType
gst_vaapidownload_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPIDOWNLOAD_H */
