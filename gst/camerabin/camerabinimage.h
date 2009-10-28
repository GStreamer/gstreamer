/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

#ifndef __CAMERABIN_IMAGE_H__
#define __CAMERABIN_IMAGE_H__

#include <gst/gstbin.h>

#include "gstcamerabin-enum.h"

G_BEGIN_DECLS
#define GST_TYPE_CAMERABIN_IMAGE             (gst_camerabin_image_get_type())
#define GST_CAMERABIN_IMAGE_CAST(obj)        ((GstCameraBinImage*)(obj))
#define GST_CAMERABIN_IMAGE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERABIN_IMAGE,GstCameraBinImage))
#define GST_CAMERABIN_IMAGE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERABIN_IMAGE,GstCameraBinImageClass))
#define GST_IS_CAMERABIN_IMAGE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERABIN_IMAGE))
#define GST_IS_CAMERABIN_IMAGE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERABIN_IMAGE))
/**
 * GstCameraBinImage:
 *
 * The opaque #GstCameraBinImage structure.
 */
typedef struct _GstCameraBinImage GstCameraBinImage;
typedef struct _GstCameraBinImageClass GstCameraBinImageClass;

struct _GstCameraBinImage
{
  GstBin parent;
  GString *filename;

  /* Ghost pads of image bin */
  GstPad *sinkpad;

  GstElement *post;
  GstElement *enc;
  GstElement *user_enc;
  GstElement *meta_mux;
  GstElement *sink;

  gboolean elements_created;
  GstCameraBinFlags flags;
};

struct _GstCameraBinImageClass
{
  GstBinClass parent_class;
};

GType gst_camerabin_image_get_type (void);

void
gst_camerabin_image_set_encoder (GstCameraBinImage * img, GstElement * encoder);

void
gst_camerabin_image_set_postproc (GstCameraBinImage * img,
    GstElement * postproc);

void
gst_camerabin_image_set_flags (GstCameraBinImage * img,
    GstCameraBinFlags flags);

GstElement *gst_camerabin_image_get_encoder (GstCameraBinImage * img);

GstElement *gst_camerabin_image_get_postproc (GstCameraBinImage * img);

G_END_DECLS
#endif                          /* #ifndef __CAMERABIN_IMAGE_H__ */
