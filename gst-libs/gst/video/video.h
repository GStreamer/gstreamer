/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2002> Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_VIDEO_H__
#define __GST_VIDEO_H__

#include <gst/gst.h>

#define R_MASK_32 0xff000000
#define G_MASK_32 0x00ff0000
#define B_MASK_32 0x0000ff00

#define R_MASK_24 0xff0000
#define G_MASK_24 0x00ff00
#define B_MASK_24 0x0000ff

#define R_MASK_16 0xf800
#define G_MASK_16 0x07e0
#define B_MASK_16 0x001f

#define R_MASK_15 0x8c00
#define G_MASK_15 0x03e0
#define B_MASK_15 0x001f

#define SIZE_RANGE GST_PROPS_INT_RANGE (16, 4096)
#define FPS_RANGE GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)

/* properties for pad templates */
#define GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32 \
        gst_props_new ( \
            "bpp",              GST_PROPS_LIST ( \
                                  GST_PROPS_INT (24), \
                                  GST_PROPS_INT (32) \
                                ), \
            "depth",            GST_PROPS_LIST ( \
                                  GST_PROPS_INT (24), \
                                  GST_PROPS_INT (32) \
                                ), \
            "endianness",       GST_PROPS_INT (G_BIG_ENDIAN), \
            "red_mask",         GST_PROPS_LIST ( \
                                  GST_PROPS_INT (R_MASK_32), \
                                  GST_PROPS_INT (R_MASK_24) \
                                ), \
            "green_mask",       GST_PROPS_LIST ( \
                                  GST_PROPS_INT (G_MASK_32), \
                                  GST_PROPS_INT (G_MASK_24) \
                                ), \
            "blue_mask",        GST_PROPS_LIST ( \
                                  GST_PROPS_INT (B_MASK_32), \
                                  GST_PROPS_INT (B_MASK_24) \
                                ), \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

#define GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_32 \
        gst_props_new ( \
            "bpp",              GST_PROPS_INT (32), \
            "depth",            GST_PROPS_INT (32), \
            "endianness",       GST_PROPS_INT (G_BIG_ENDIAN), \
            "red_mask",         GST_PROPS_INT (R_MASK_32), \
            "green_mask",       GST_PROPS_INT (G_MASK_32), \
            "blue_mask",        GST_PROPS_INT (B_MASK_32), \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

#define GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24 \
        gst_props_new ( \
            "bpp",              GST_PROPS_INT (24), \
            "depth",            GST_PROPS_INT (24), \
            "endianness",       GST_PROPS_INT (G_BIG_ENDIAN), \
            "red_mask",         GST_PROPS_INT (R_MASK_24), \
            "green_mask",       GST_PROPS_INT (G_MASK_24), \
            "blue_mask",        GST_PROPS_INT (B_MASK_24), \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

#define GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_15_16 \
        gst_props_new ( \
            "bpp",              GST_PROPS_INT (16), \
            "depth",            GST_PROPS_LIST ( \
                                  GST_PROPS_INT (15), \
                                  GST_PROPS_INT (16) \
                                ), \
            "endianness",       GST_PROPS_INT (G_BYTE_ORDER), \
            "red_mask",         GST_PROPS_LIST ( \
                                  GST_PROPS_INT (R_MASK_15), \
                                  GST_PROPS_INT (R_MASK_16) \
                                ), \
            "green_mask",       GST_PROPS_LIST ( \
                                  GST_PROPS_INT (G_MASK_15), \
                                  GST_PROPS_INT (G_MASK_16) \
                                ), \
            "blue_mask",        GST_PROPS_LIST ( \
                                  GST_PROPS_INT (B_MASK_15), \
                                  GST_PROPS_INT (B_MASK_16) \
                                ), \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

#define GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_16 \
        gst_props_new ( \
            "bpp",              GST_PROPS_INT (16), \
            "depth",            GST_PROPS_INT (16), \
            "endianness",       GST_PROPS_INT (G_BYTE_ORDER), \
            "red_mask",         GST_PROPS_INT (R_MASK_16), \
            "green_mask",       GST_PROPS_INT (G_MASK_16), \
            "blue_mask",        GST_PROPS_INT (B_MASK_16), \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

#define GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_15 \
        gst_props_new ( \
            "bpp",              GST_PROPS_INT (15), \
            "depth",            GST_PROPS_INT (15), \
            "endianness",       GST_PROPS_INT (G_BYTE_ORDER), \
            "red_mask",         GST_PROPS_INT (R_MASK_15), \
            "green_mask",       GST_PROPS_INT (G_MASK_15), \
            "blue_mask",        GST_PROPS_INT (B_MASK_15), \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

#define GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(fourcc) \
        gst_props_new (\
            "format",           fourcc, \
            "width",            SIZE_RANGE, \
            "height",           SIZE_RANGE, \
            "framerate",        FPS_RANGE, \
            NULL)

/* functions */
gfloat   gst_video_frame_rate (GstPad *pad);
gboolean gst_video_get_size   (GstPad *pad,
                               gint   *width,
                               gint   *height);

#endif /* __GST_VIDEO_H__ */
