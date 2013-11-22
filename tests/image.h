/*
 *  image.h - Image utilities for the tests
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
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

#ifndef IMAGE_H
#define IMAGE_H

#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapisurface.h>

GstVaapiImage *
image_generate(
    GstVaapiDisplay    *display,
    GstVideoFormat      format,
    guint               width,
    guint               height
);

GstVaapiImage *
image_generate_full(
    GstVaapiDisplay    *display,
    GstVideoFormat      format,
    guint               width,
    guint               height,
    guint32             flags
);

gboolean
image_upload(GstVaapiImage *image, GstVaapiSurface *surface);

#endif /* IMAGE_H */
