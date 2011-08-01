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

#include "gst/video/gstvideopool.h"

/**
 * gst_buffer_pool_config_set_video_alignment:
 * @config: a #GstStructure
 * @align: a #GstVideoAlignment
 *
 * Set the video alignment in @align to the bufferpool configuration
 * @config
 */
void
gst_buffer_pool_config_set_video_alignment (GstStructure * config,
    GstVideoAlignment * align)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (align != NULL);

  gst_structure_set (config,
      "padding-top", G_TYPE_UINT, align->padding_top,
      "padding-bottom", G_TYPE_UINT, align->padding_bottom,
      "padding-left", G_TYPE_UINT, align->padding_left,
      "padding-right", G_TYPE_UINT, align->padding_right,
      "stride-align0", G_TYPE_UINT, align->stride_align[0],
      "stride-align1", G_TYPE_UINT, align->stride_align[1],
      "stride-align2", G_TYPE_UINT, align->stride_align[2],
      "stride-align3", G_TYPE_UINT, align->stride_align[3], NULL);
}

/**
 * gst_buffer_pool_config_get_video_alignment:
 * @config: a #GstStructure
 * @align: a #GstVideoAlignment
 *
 * Get the video alignment from the bufferpool configuration @config in
 * in @align
 *
 * Returns: #TRUE if @config could be parsed correctly.
 */
gboolean
gst_buffer_pool_config_get_video_alignment (GstStructure * config,
    GstVideoAlignment * align)
{
  g_return_val_if_fail (config != NULL, FALSE);
  g_return_val_if_fail (align != NULL, FALSE);

  return gst_structure_get (config,
      "padding-top", G_TYPE_UINT, &align->padding_top,
      "padding-bottom", G_TYPE_UINT, &align->padding_bottom,
      "padding-left", G_TYPE_UINT, &align->padding_left,
      "padding-right", G_TYPE_UINT, &align->padding_right,
      "stride-align0", G_TYPE_UINT, &align->stride_align[0],
      "stride-align1", G_TYPE_UINT, &align->stride_align[1],
      "stride-align2", G_TYPE_UINT, &align->stride_align[2],
      "stride-align3", G_TYPE_UINT, &align->stride_align[3], NULL);
}
