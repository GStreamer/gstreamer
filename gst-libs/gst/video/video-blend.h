/* Gstreamer video blending utility functions
 *
 * Copyright (C) <2011> Intel Corporation
 * Copyright (C) <2011> Collabora Ltd.
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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


#ifndef  __GST_VIDEO_BLEND__
#define  __GST_VIDEO_BLEND__

#include <gst/gst.h>
#include <gst/video/video.h>

#define MAX_VIDEO_PLANES 4

typedef struct _GstBlendVideoFormatInfo GstBlendVideoFormatInfo;

/* GstBlendVideoFormatInfo:
 * @fmt: The #GstVideoFormat describing the video format
 * @width: The width of the video
 * @height: The height of the video
 * @pixels: The buffer containing the pixels of the video
 * @size: The size in byte of @pixels
 * @offset: The offsets of the different component of the video
 * @stride: The stride of the different component of the video
 *
 * Information describing image properties containing necessary
 * fields to do video blending.
 */
struct _GstBlendVideoFormatInfo
{
    GstVideoFormat  fmt;

    gint            width;
    gint            height;

    guint8        * pixels;
    gsize           size;

    /* YUV components: Y=0, U=1, V=2, A=3
     * RGB components: R=0, G=1, B=2, A=3 */
    gint            offset[MAX_VIDEO_PLANES];
    gint            stride[MAX_VIDEO_PLANES];
};

void       video_blend_format_info_init   (GstBlendVideoFormatInfo * info,
                                           guint8 *pixels, guint height,
                                           guint width, GstVideoFormat fmt);

void       video_blend_scale_linear_RGBA  (GstBlendVideoFormatInfo * src,
                                           gint dest_height, gint dest_width);

gboolean   video_blend                    (GstBlendVideoFormatInfo * dest,
                                           GstBlendVideoFormatInfo * src,
                                           guint x, guint y);

#endif
