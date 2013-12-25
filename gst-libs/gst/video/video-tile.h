/* GStreamer
 * Copyright (C) <2013> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_VIDEO_TILE_H__
#define __GST_VIDEO_TILE_H__

#include <gst/gst.h>

#include <gst/video/video-format.h>

G_BEGIN_DECLS

/**
 * GstVideoTileType:
 * @GST_VIDEO_TILE_TYPE_INDEXED: Tiles are indexed. Use
 *   gst_video_tile_get_index () to retrieve the tile at the requested
 *   coordinates.
 *
 * Enum value describing the most common tiling types.
 */
typedef enum
{
  GST_VIDEO_TILE_TYPE_INDEXED = 0
} GstVideoTileType;

#define GST_VIDEO_TILE_TYPE_SHIFT     (16)
#define GST_VIDEO_TILE_TYPE_MASK      ((1 << GST_VIDEO_TILE_TYPE_SHIFT) - 1)

/**
 * GST_VIDEO_TILE_MAKE_MODE:
 * @num: the mode number to create
 * @type: the tile mode type
 *
 * use this macro to create new tile modes.
 */
#define GST_VIDEO_TILE_MAKE_MODE(num, type) \
    (((num) << GST_VIDEO_TILE_TYPE_SHIFT) | (GST_VIDEO_TILE_TYPE_ ##type))

/**
 * GST_VIDEO_TILE_MODE_TYPE:
 * @mode: the tile mode
 *
 * Get the tile mode type of @mode
 */
#define GST_VIDEO_TILE_MODE_TYPE(mode)       ((mode) & GST_VIDEO_TILE_TYPE_MASK)

/**
 * GST_VIDEO_TILE_MODE_IS_INDEXED:
 * @mode: a tile mode
 *
 * Check if @mode is an indexed tile type
 */
#define GST_VIDEO_TILE_MODE_IS_INDEXED(mode) (GST_VIDEO_TILE_MODE_TYPE(mode) == GST_VIDEO_TILE_TYPE_INDEXED)

/**
 * GstVideoTileMode:
 * @GST_VIDEO_TILE_MODE_UNKNOWN: Unknown or unset tile mode
 * @GST_VIDEO_TILE_MODE_ZFLIPZ_2X2: Every four adjacent buffers - two
 *    horizontally and two vertically are grouped together and are located
 *    in memory in Z or flipped Z order.
 *
 * Enum value describing the available tiling modes.
 */
typedef enum
{
  GST_VIDEO_TILE_MODE_UNKNOWN = 0,
  GST_VIDEO_TILE_MODE_ZFLIPZ_2X2 = GST_VIDEO_TILE_MAKE_MODE (1, INDEXED),
} GstVideoTileMode;

guint           gst_video_tile_get_index                (GstVideoTileMode mode, gint x, gint y,
                                                         gint x_tiles, gint y_tiles);


G_END_DECLS

#endif /* __GST_VIDEO_TILE_H__ */
