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

#define GST_VIDEO_BYTE1_MASK_32  "0xFF000000"
#define GST_VIDEO_BYTE2_MASK_32  "0x00FF0000"
#define GST_VIDEO_BYTE3_MASK_32  "0x0000FF00"
#define GST_VIDEO_BYTE4_MASK_32  "0x000000FF"

#define GST_VIDEO_BYTE1_MASK_24  "0x00FF0000"
#define GST_VIDEO_BYTE2_MASK_24  "0x0000FF00"
#define GST_VIDEO_BYTE3_MASK_24  "0x000000FF"

#define GST_VIDEO_BYTE1_MASK_32_INT  0xFF000000
#define GST_VIDEO_BYTE2_MASK_32_INT  0x00FF0000
#define GST_VIDEO_BYTE3_MASK_32_INT  0x0000FF00
#define GST_VIDEO_BYTE4_MASK_32_INT  0x000000FF

#define GST_VIDEO_BYTE1_MASK_24_INT  0x00FF0000
#define GST_VIDEO_BYTE2_MASK_24_INT  0x0000FF00
#define GST_VIDEO_BYTE3_MASK_24_INT  0x000000FF

#define GST_VIDEO_RED_MASK_16 "0xf800"
#define GST_VIDEO_GREEN_MASK_16 "0x07e0"
#define GST_VIDEO_BLUE_MASK_16 "0x001f"

#define GST_VIDEO_RED_MASK_15 "0x7c00"
#define GST_VIDEO_GREEN_MASK_15 "0x03e0"
#define GST_VIDEO_BLUE_MASK_15 "0x001f"

#define GST_VIDEO_RED_MASK_16_INT 0xf800
#define GST_VIDEO_GREEN_MASK_16_INT 0x07e0
#define GST_VIDEO_BLUE_MASK_16_INT 0x001f

#define GST_VIDEO_RED_MASK_15_INT 0x7c00
#define GST_VIDEO_GREEN_MASK_15_INT 0x03e0
#define GST_VIDEO_BLUE_MASK_15_INT 0x001f

#define GST_VIDEO_SIZE_RANGE "(int) [ 16, 4096 ]"
#define GST_VIDEO_FPS_RANGE "(double) [ 0, max ]"

/* consider the next 2 protected */
#define __GST_VIDEO_CAPS_MAKE_32(R, G, B)				\
    "video/x-raw-rgb, "							\
    "bpp = (int) 32, "							\
    "depth = (int) 24, "						\
    "endianness = (int) BIG_ENDIAN, "					\
    "red_mask = (int) " GST_VIDEO_BYTE ## R ## _MASK_32 ", "		\
    "green_mask = (int) " GST_VIDEO_BYTE ## G ## _MASK_32 ", "		\
    "blue_mask = (int) " GST_VIDEO_BYTE ## B ## _MASK_32 ", "		\
    "width = " GST_VIDEO_SIZE_RANGE ", "				\
    "height = " GST_VIDEO_SIZE_RANGE ", "				\
    "framerate = " GST_VIDEO_FPS_RANGE

#define __GST_VIDEO_CAPS_MAKE_24(R, G, B)				\
    "video/x-raw-rgb, "							\
    "bpp = (int) 24, "							\
    "depth = (int) 24, "						\
    "endianness = (int) BIG_ENDIAN, "					\
    "red_mask = (int) " GST_VIDEO_BYTE ## R ## _MASK_24 ", "		\
    "green_mask = (int) " GST_VIDEO_BYTE ## G ## _MASK_24 ", "		\
    "blue_mask = (int) " GST_VIDEO_BYTE ## B ## _MASK_24 ", "		\
    "width = " GST_VIDEO_SIZE_RANGE ", "				\
    "height = " GST_VIDEO_SIZE_RANGE ", "				\
    "framerate = " GST_VIDEO_FPS_RANGE


/* 24 bit */

#define GST_VIDEO_CAPS_RGB \
    __GST_VIDEO_CAPS_MAKE_24 (1, 2, 3)

#define GST_VIDEO_CAPS_BGR \
    __GST_VIDEO_CAPS_MAKE_24 (3, 2, 1)

/* 32 bit */

#define GST_VIDEO_CAPS_RGBx \
    __GST_VIDEO_CAPS_MAKE_32 (1, 2, 3)
  
#define GST_VIDEO_CAPS_xRGB \
    __GST_VIDEO_CAPS_MAKE_32 (2, 3, 4)
  
#define GST_VIDEO_CAPS_BGRx \
    __GST_VIDEO_CAPS_MAKE_32 (3, 2, 1)
  
#define GST_VIDEO_CAPS_xBGR \
    __GST_VIDEO_CAPS_MAKE_32 (4, 3, 2)

/* note: the macro name uses the order on BE systems */
#if G_BYTE_ORDER == G_BIG_ENDIAN
  #define GST_VIDEO_CAPS_xRGB_HOST_ENDIAN \
      GST_VIDEO_CAPS_xRGB
  #define GST_VIDEO_CAPS_BGRx_HOST_ENDIAN \
      GST_VIDEO_CAPS_BGRx
#else
  #define GST_VIDEO_CAPS_xRGB_HOST_ENDIAN \
      GST_VIDEO_CAPS_BGRx
  #define GST_VIDEO_CAPS_BGRx_HOST_ENDIAN \
      GST_VIDEO_CAPS_xRGB
#endif
      
/* 15/16 bit */
  
#define GST_VIDEO_CAPS_RGB_16						\
	    "video/x-raw-rgb, "						\
            "bpp = (int) 16, "						\
            "depth = (int) 16, "					\
            "endianness = (int) BYTE_ORDER, "				\
            "red_mask = (int) " GST_VIDEO_RED_MASK_16 ", "		\
            "green_mask = (int) " GST_VIDEO_GREEN_MASK_16 ", "		\
            "blue_mask = (int) " GST_VIDEO_BLUE_MASK_16 ", "		\
            "width = " GST_VIDEO_SIZE_RANGE ", "			\
            "height = " GST_VIDEO_SIZE_RANGE ", "			\
            "framerate = " GST_VIDEO_FPS_RANGE

#define GST_VIDEO_CAPS_RGB_15						\
	    "video/x-raw-rgb, "						\
            "bpp = (int) 16, "						\
            "depth = (int) 15, "					\
            "endianness = (int) BYTE_ORDER, "				\
            "red_mask = (int) " GST_VIDEO_RED_MASK_15 ", "		\
            "green_mask = (int) " GST_VIDEO_GREEN_MASK_15 ", "		\
            "blue_mask = (int) " GST_VIDEO_BLUE_MASK_15 ", "		\
            "width = " GST_VIDEO_SIZE_RANGE ", "			\
            "height = " GST_VIDEO_SIZE_RANGE ", "			\
            "framerate = " GST_VIDEO_FPS_RANGE

#define GST_VIDEO_CAPS_YUV(fourcc)					\
	"video/x-raw-yuv, "						\
	"format = (fourcc) " fourcc ", "				\
	"width = " GST_VIDEO_SIZE_RANGE ", "				\
	"height = " GST_VIDEO_SIZE_RANGE ", "				\
	"framerate = " GST_VIDEO_FPS_RANGE

/* functions */
gdouble  gst_video_frame_rate (GstPad *pad);
gboolean gst_video_get_size   (GstPad *pad,
                               gint   *width,
                               gint   *height);

#endif /* __GST_VIDEO_H__ */
