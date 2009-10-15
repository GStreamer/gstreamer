/*
* GStreamer
* Copyright (C) 2009 Nokia Corporation <multimedia@maemo.org>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifndef __CAMERABINPREVIEW_H__
#define __CAMERABINPREVIEW_H__

#include <gst/gst.h>

#include "gstcamerabin.h"

G_BEGIN_DECLS
    GstElement * gst_camerabin_preview_create_pipeline (GstCameraBin * camera,
    GstCaps * caps);

void gst_camerabin_preview_destroy_pipeline (GstCameraBin * camera,
    GstElement * pipeline);

GstBuffer *gst_camerabin_preview_convert (GstCameraBin * camera,
    GstElement * pipeline, GstBuffer * buf);

G_END_DECLS
#endif                          /* __CAMERABINPREVIEW_H__ */
