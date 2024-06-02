/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SCKIT_VIDEO_SRC_H__
#define __GST_SCKIT_VIDEO_SRC_H__

#include <gst/base/gstbasesrc.h>

#include "sckitsrc.h"
#include "sckitvideosrc-shared.h"

G_BEGIN_DECLS
#define GST_TYPE_SCKIT_VIDEO_SRC (gst_sckit_video_src_get_type())
#define GST_SCKIT_VIDEO_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SCKIT_VIDEO_SRC, GstSCKitVideoSrc))
#define GST_SCKIT_VIDEO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SCKIT_VIDEO_SRC, GstSCKitVideoSrcClass))
#define GST_IS_SCKIT_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SCKIT_VIDEO_SRC))
#define GST_IS_SCKIT_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SCKIT_VIDEO_SRC))

typedef struct _GstSCKitVideoSrc GstSCKitVideoSrc;
typedef struct _GstSCKitVideoSrcClass GstSCKitVideoSrcClass;

struct API_AVAILABLE(macos(12.3)) _GstSCKitVideoSrc
{
  GstBaseSrc src;
  SCKitVideoSrc *impl;
};

struct API_AVAILABLE(macos(12.3)) _GstSCKitVideoSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_sckit_video_src_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (sckitaudiosrc);

G_END_DECLS

#endif /* __GST_SCKIT_VIDEO_SRC_H__ */
