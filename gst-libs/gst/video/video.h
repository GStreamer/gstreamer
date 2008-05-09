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

G_BEGIN_DECLS

/**
 * GstVideoFormat:
 * @GST_VIDEO_FORMAT_UNKNOWN: Unknown or unset video format id
 * @GST_VIDEO_FORMAT_I420: planar 4:2:0 YUV
 * @GST_VIDEO_FORMAT_YV12: planar 4:2:0 YVU (like I420 but UV planes swapped)
 * @GST_VIDEO_FORMAT_YUY2: packed 4:2:2 YUV (Y0-U0-Y1-V0 Y2-U2-Y3-V2 Y4 ...)
 * @GST_VIDEO_FORMAT_UYVY: packed 4:2:2 YUV (U0-Y0-V0-Y1 U2-Y2-V2-Y3 U4 ...) 
 * @GST_VIDEO_FORMAT_AYUV: packed 4:4:4 YUV with alpha channel (A0-Y0-U0-V0 ...)
 * @GST_VIDEO_FORMAT_RGBx:
 * @GST_VIDEO_FORMAT_BGRx:
 * @GST_VIDEO_FORMAT_xRGB:
 * @GST_VIDEO_FORMAT_xBGR:
 * @GST_VIDEO_FORMAT_RGBA:
 * @GST_VIDEO_FORMAT_BGRA:
 * @GST_VIDEO_FORMAT_ARGB:
 * @GST_VIDEO_FORMAT_ABGR:
 * @GST_VIDEO_FORMAT_RGB:
 * @GST_VIDEO_FORMAT_BGR:
 * @GST_VIDEO_FORMAT_Y41B: planar 4:1:1 YUV (Since: 0.10.18)
 * @GST_VIDEO_FORMAT_Y42B: planar 4:2:2 YUV (Since: 0.10.18)
 *
 * Enum value describing the most common video formats.
 */
typedef enum {
  GST_VIDEO_FORMAT_UNKNOWN,
  GST_VIDEO_FORMAT_I420,
  GST_VIDEO_FORMAT_YV12,
  GST_VIDEO_FORMAT_YUY2,
  GST_VIDEO_FORMAT_UYVY,
  GST_VIDEO_FORMAT_AYUV,
  GST_VIDEO_FORMAT_RGBx,
  GST_VIDEO_FORMAT_BGRx,
  GST_VIDEO_FORMAT_xRGB,
  GST_VIDEO_FORMAT_xBGR,
  GST_VIDEO_FORMAT_RGBA,
  GST_VIDEO_FORMAT_BGRA,
  GST_VIDEO_FORMAT_ARGB,
  GST_VIDEO_FORMAT_ABGR,
  GST_VIDEO_FORMAT_RGB,
  GST_VIDEO_FORMAT_BGR,
  GST_VIDEO_FORMAT_Y41B,
  GST_VIDEO_FORMAT_Y42B
} GstVideoFormat;

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

#define GST_VIDEO_SIZE_RANGE "(int) [ 1, max ]"
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, max ]"

/* consider the next 2 protected */
#define __GST_VIDEO_CAPS_MAKE_32A(R, G, B, A)                           \
    "video/x-raw-rgb, "                                                 \
    "bpp = (int) 32, "                                                  \
    "depth = (int) 32, "                                                \
    "endianness = (int) BIG_ENDIAN, "                                   \
    "red_mask = (int) " GST_VIDEO_BYTE ## R ## _MASK_32 ", "            \
    "green_mask = (int) " GST_VIDEO_BYTE ## G ## _MASK_32 ", "          \
    "blue_mask = (int) " GST_VIDEO_BYTE ## B ## _MASK_32 ", "           \
    "alpha_mask = (int) " GST_VIDEO_BYTE ## A ## _MASK_32 ", "          \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE

#define __GST_VIDEO_CAPS_MAKE_32(R, G, B)                               \
    "video/x-raw-rgb, "                                                 \
    "bpp = (int) 32, "                                                  \
    "depth = (int) 24, "                                                \
    "endianness = (int) BIG_ENDIAN, "                                   \
    "red_mask = (int) " GST_VIDEO_BYTE ## R ## _MASK_32 ", "            \
    "green_mask = (int) " GST_VIDEO_BYTE ## G ## _MASK_32 ", "          \
    "blue_mask = (int) " GST_VIDEO_BYTE ## B ## _MASK_32 ", "           \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE

#define __GST_VIDEO_CAPS_MAKE_24(R, G, B)                               \
    "video/x-raw-rgb, "                                                 \
    "bpp = (int) 24, "                                                  \
    "depth = (int) 24, "                                                \
    "endianness = (int) BIG_ENDIAN, "                                   \
    "red_mask = (int) " GST_VIDEO_BYTE ## R ## _MASK_24 ", "            \
    "green_mask = (int) " GST_VIDEO_BYTE ## G ## _MASK_24 ", "          \
    "blue_mask = (int) " GST_VIDEO_BYTE ## B ## _MASK_24 ", "           \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
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

/* 32 bit alpha */

#define GST_VIDEO_CAPS_RGBA \
    __GST_VIDEO_CAPS_MAKE_32A (1, 2, 3, 4)
  
#define GST_VIDEO_CAPS_ARGB \
    __GST_VIDEO_CAPS_MAKE_32A (2, 3, 4, 1)
  
#define GST_VIDEO_CAPS_BGRA \
    __GST_VIDEO_CAPS_MAKE_32A (3, 2, 1, 4)
  
#define GST_VIDEO_CAPS_ABGR \
    __GST_VIDEO_CAPS_MAKE_32A (4, 3, 2, 1)

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
  
#define GST_VIDEO_CAPS_RGB_16                                           \
            "video/x-raw-rgb, "                                         \
            "bpp = (int) 16, "                                          \
            "depth = (int) 16, "                                        \
            "endianness = (int) BYTE_ORDER, "                           \
            "red_mask = (int) " GST_VIDEO_RED_MASK_16 ", "              \
            "green_mask = (int) " GST_VIDEO_GREEN_MASK_16 ", "          \
            "blue_mask = (int) " GST_VIDEO_BLUE_MASK_16 ", "            \
            "width = " GST_VIDEO_SIZE_RANGE ", "                        \
            "height = " GST_VIDEO_SIZE_RANGE ", "                       \
            "framerate = " GST_VIDEO_FPS_RANGE

#define GST_VIDEO_CAPS_RGB_15                                           \
            "video/x-raw-rgb, "                                         \
            "bpp = (int) 16, "                                          \
            "depth = (int) 15, "                                        \
            "endianness = (int) BYTE_ORDER, "                           \
            "red_mask = (int) " GST_VIDEO_RED_MASK_15 ", "              \
            "green_mask = (int) " GST_VIDEO_GREEN_MASK_15 ", "          \
            "blue_mask = (int) " GST_VIDEO_BLUE_MASK_15 ", "            \
            "width = " GST_VIDEO_SIZE_RANGE ", "                        \
            "height = " GST_VIDEO_SIZE_RANGE ", "                       \
            "framerate = " GST_VIDEO_FPS_RANGE

#define GST_VIDEO_CAPS_YUV(fourcc)                                      \
        "video/x-raw-yuv, "                                             \
        "format = (fourcc) " fourcc ", "                                \
        "width = " GST_VIDEO_SIZE_RANGE ", "                            \
        "height = " GST_VIDEO_SIZE_RANGE ", "                           \
        "framerate = " GST_VIDEO_FPS_RANGE

/* functions */
const GValue *gst_video_frame_rate (GstPad *pad);
gboolean gst_video_get_size   (GstPad *pad,
                               gint   *width,
                               gint   *height);

gboolean gst_video_calculate_display_ratio (guint *dar_n, guint *dar_d,
            guint video_width, guint video_height, 
            guint video_par_n, guint video_par_d, 
            guint display_par_n, guint display_par_d);

gboolean gst_video_format_parse_caps (GstCaps *caps, GstVideoFormat *format,
    int *width, int *height);
gboolean gst_video_parse_caps_framerate (GstCaps *caps,
    int *fps_n, int *fps_d);
gboolean gst_video_parse_caps_pixel_aspect_ratio (GstCaps *caps,
    int *par_n, int *par_d);
GstCaps * gst_video_format_new_caps (GstVideoFormat format,
    int width, int height, int framerate_n, int framerate_d,
    int par_n, int par_d);
GstVideoFormat gst_video_format_from_fourcc (guint32 fourcc);
guint32 gst_video_format_to_fourcc (GstVideoFormat format);
gboolean gst_video_format_is_rgb (GstVideoFormat format);
gboolean gst_video_format_is_yuv (GstVideoFormat format);
gboolean gst_video_format_has_alpha (GstVideoFormat format);
int gst_video_format_get_row_stride (GstVideoFormat format, int component,
    int width);
int gst_video_format_get_pixel_stride (GstVideoFormat format, int component);
int gst_video_format_get_component_width (GstVideoFormat format, int component,
    int width);
int gst_video_format_get_component_height (GstVideoFormat format, int component,
    int height);
int gst_video_format_get_component_offset (GstVideoFormat format, int component,
    int width, int height);
int gst_video_format_get_size (GstVideoFormat format, int width, int height);
gboolean gst_video_format_convert (GstVideoFormat format, int width, int height,
    int fps_n, int fps_d,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value);


G_END_DECLS

#endif /* __GST_VIDEO_H__ */
