/* GStreamer
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_VIDEO_POOL_H__
#define __GST_VIDEO_POOL_H__

#include <gst/gst.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GST_BUFFER_POOL_OPTION_META_VIDEO:
 *
 * An option that can be activated on bufferpool to request video metadata
 * on buffers from the pool.
 */
#define GST_BUFFER_POOL_OPTION_META_VIDEO "GstBufferPoolOptionMetaVideo"

/**
 * GST_BUFFER_POOL_OPTION_VIDEO_LAYOUT:
 *
 * A bufferpool option to enable extra padding. When a bufferpool supports this
 * option, gst_buffer_pool_set_video_layout() can be called.
 */
#define GST_BUFFER_POOL_OPTION_VIDEO_LAYOUT "GstBufferPoolOptionVideoLayout"

typedef struct _GstBufferPoolOptionVideoLayout GstBufferPoolOptionVideoLayout;

/**
 * GstBufferPoolOptionVideoLayout:
 * @padding_left: extra pixels on the left side
 * @padding_right: extra pixels on the right side
 * @padding_top: extra pixels on the top
 * @padding_bottom: extra pixels on the bottom
 * @stride_align: array with extra alignment requirements for the strides
 *
 * Extra parameters to configure the memory layout for video buffers. This
 * structure is used to configure the bufferpool if it supports the
 * #GST_BUFFER_POOL_OPTION_VIDEO_LAYOUT.
 */
struct _GstBufferPoolOptionVideoLayout
{
  guint padding_left;
  guint padding_right;
  guint padding_top;
  guint padding_bottom;
  gint stride_align[GST_VIDEO_MAX_PLANES];
};

void             gst_buffer_pool_config_set_video_layout  (GstStructure *config,
                                                           GstBufferPoolOptionVideoLayout *layout);
gboolean         gst_buffer_pool_config_get_video_layout  (GstStructure *config,
                                                           GstBufferPoolOptionVideoLayout *layout);


G_END_DECLS

#endif /* __GST_VIDEO_POOL_H__ */
