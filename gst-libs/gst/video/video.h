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

#define R_MASK_32 "0xff000000"
#define G_MASK_32 "0x00ff0000"
#define B_MASK_32 "0x0000ff00"

#define R_MASK_32_REVERSE "0x000000ff"
#define G_MASK_32_REVERSE "0x0000ff00"
#define B_MASK_32_REVERSE "0x00ff0000"

#define R_MASK_24 "0xff0000"
#define G_MASK_24 "0x00ff00"
#define B_MASK_24 "0x0000ff"

#define R_MASK_24_REVERSE "0x0000ff"
#define G_MASK_24_REVERSE "0x00ff00"
#define B_MASK_24_REVERSE "0xff0000"

#define R_MASK_16 "0xf800"
#define G_MASK_16 "0x07e0"
#define B_MASK_16 "0x001f"

#define R_MASK_15 "0x7c00"
#define G_MASK_15 "0x03e0"
#define B_MASK_15 "0x001f"

#define R_MASK_32_INT 0xff000000
#define G_MASK_32_INT 0x00ff0000
#define B_MASK_32_INT 0x0000ff00

#define R_MASK_32_REVERSE_INT 0x000000ff
#define G_MASK_32_REVERSE_INT 0x0000ff00
#define B_MASK_32_REVERSE_INT 0x00ff0000

#define R_MASK_24_INT 0xff0000
#define G_MASK_24_INT 0x00ff00
#define B_MASK_24_INT 0x0000ff

#define R_MASK_24_REVERSE_INT 0x0000ff
#define G_MASK_24_REVERSE_INT 0x00ff00
#define B_MASK_24_REVERSE_INT 0xff0000

#define R_MASK_16_INT 0xf800
#define G_MASK_16_INT 0x07e0
#define B_MASK_16_INT 0x001f

#define R_MASK_15_INT 0x7c00
#define G_MASK_15_INT 0x03e0
#define B_MASK_15_INT 0x001f

#define SIZE_RANGE "(int) [ 16, 4096 ]"
#define FPS_RANGE "(double) [ 0, max ]"

/* properties for pad templates */
#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24_32 \
	    "video/x-raw-rgb, " \
            "bpp = (int) { 24, 32 }, " \
            "depth = (int) { 24, 32 }, " \
            "endianness = (int) BIG_ENDIAN, " \
            "red_mask = (int) { " R_MASK_32 ", " R_MASK_24 " }, " \
            "green_mask = (int) { " G_MASK_32 ", " G_MASK_24 " }, " \
            "blue_mask = (int) { " B_MASK_32 ", " B_MASK_24 " }, " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24_32_REVERSE \
	    "video/x-raw-rgb, " \
            "bpp = (int) { 24, 32 }, " \
            "depth = (int) { 24, 32 }, " \
            "endianness = (int) BIG_ENDIAN, " \
            "red_mask = (int) { " R_MASK_32_REVERSE ", " R_MASK_24_REVERSE "}, " \
            "green_mask = (int) { " G_MASK_32_REVERSE ", " G_MASK_24_REVERSE "}, " \
            "blue_mask = (int) { " B_MASK_32_REVERSE ", " B_MASK_24_REVERSE "}, " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_32 \
	    "video/x-raw-rgb, " \
            "bpp = (int) 32, " \
            "depth = (int) 32, " \
            "endianness = (int) BIG_ENDIAN, " \
            "red_mask = (int) " R_MASK_32 ", " \
            "green_mask = (int) " G_MASK_32 ", " \
            "blue_mask = (int) " B_MASK_32 ", " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24 \
	    "video/x-raw-rgb, " \
            "bpp = (int) 24, " \
            "depth = (int) 24, " \
            "endianness = (int) BIG_ENDIAN, " \
            "red_mask = (int) " R_MASK_24 ", " \
            "green_mask = (int) " G_MASK_24 ", " \
            "blue_mask = (int) " B_MASK_24 ", " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_32_REVERSE \
	    "video/x-raw-rgb, " \
            "bpp = (int) 32, " \
            "depth = (int) 32, " \
            "endianness = (int) BIG_ENDIAN, " \
            "red_mask = (int) " R_MASK_32_REVERSE ", " \
            "green_mask = (int) " G_MASK_32_REVERSE ", " \
            "blue_mask = (int) " B_MASK_32_REVERSE ", " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24_REVERSE \
	    "video/x-raw-rgb, " \
            "bpp = (int) 24, " \
            "depth = (int) 24, " \
            "endianness = (int) BIG_ENDIAN, " \
            "red_mask = (int) " R_MASK_24_REVERSE ", " \
            "green_mask = (int) " G_MASK_24_REVERSE ", " \
            "blue_mask = (int) " B_MASK_24_REVERSE ", " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_15_16 \
	    "video/x-raw-rgb, " \
            "bpp = (int) 16, " \
            "depth = (int) { 15, 16 }, " \
            "endianness = (int) BYTE_ORDER, " \
            "red_mask = (int) { " R_MASK_15 ", " R_MASK_16 " }, " \
            "green_mask = (int) { " G_MASK_15 ", " G_MASK_16 " }, " \
            "blue_mask = (int) { " B_MASK_15 ", " B_MASK_16 " }, " \
            "width = " SIZE_RANGE ", " \
            "height = " SIZE_RANGE ", " \
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_16 \
	    "video/x-raw-rgb, "				\
            "bpp = (int) 16, "				\
            "depth = (int) 16, "			\
            "endianness = (int) BYTE_ORDER, "		\
            "red_mask = (int) " R_MASK_16 ", "		\
            "green_mask = (int) " G_MASK_16 ", "	\
            "blue_mask = (int) " B_MASK_16 ", "		\
            "width = " SIZE_RANGE ", "			\
            "height = " SIZE_RANGE ", "			\
            "framerate = " FPS_RANGE

#define GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_15 \
	    "video/x-raw-rgb, "				\
            "bpp = (int) 16, "				\
            "depth = (int) 15, "			\
            "endianness = (int) BYTE_ORDER, "		\
            "red_mask = (int) " R_MASK_15 ", "		\
            "green_mask = (int) " G_MASK_15 ", "	\
            "blue_mask = (int) " B_MASK_15 ", "		\
            "width = " SIZE_RANGE ", "			\
            "height = " SIZE_RANGE ", "			\
            "framerate = " FPS_RANGE

#define GST_VIDEO_YUV_PAD_TEMPLATE_CAPS(fourcc) \
	"video/x-raw-yuv, " \
	"format = (fourcc) " fourcc ", " \
	"width = " SIZE_RANGE ", " \
	"height = " SIZE_RANGE ", " \
	"framerate = " FPS_RANGE

/* functions */
gdouble  gst_video_frame_rate (GstPad *pad);
gboolean gst_video_get_size   (GstPad *pad,
                               gint   *width,
                               gint   *height);

#endif /* __GST_VIDEO_H__ */
