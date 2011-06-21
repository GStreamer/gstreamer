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

#include <gst/video/video-enumtypes.h>

/**
 * GstVideoFormat:
 * @GST_VIDEO_FORMAT_UNKNOWN: Unknown or unset video format id
 * @GST_VIDEO_FORMAT_I420: planar 4:2:0 YUV
 * @GST_VIDEO_FORMAT_YV12: planar 4:2:0 YVU (like I420 but UV planes swapped)
 * @GST_VIDEO_FORMAT_YUY2: packed 4:2:2 YUV (Y0-U0-Y1-V0 Y2-U2-Y3-V2 Y4 ...)
 * @GST_VIDEO_FORMAT_UYVY: packed 4:2:2 YUV (U0-Y0-V0-Y1 U2-Y2-V2-Y3 U4 ...)
 * @GST_VIDEO_FORMAT_AYUV: packed 4:4:4 YUV with alpha channel (A0-Y0-U0-V0 ...)
 * @GST_VIDEO_FORMAT_RGBx: sparse rgb packed into 32 bit, space last
 * @GST_VIDEO_FORMAT_BGRx: sparse reverse rgb packed into 32 bit, space last
 * @GST_VIDEO_FORMAT_xRGB: sparse rgb packed into 32 bit, space first
 * @GST_VIDEO_FORMAT_xBGR: sparse reverse rgb packed into 32 bit, space first
 * @GST_VIDEO_FORMAT_RGBA: rgb with alpha channel last
 * @GST_VIDEO_FORMAT_BGRA: reverse rgb with alpha channel last
 * @GST_VIDEO_FORMAT_ARGB: rgb with alpha channel first
 * @GST_VIDEO_FORMAT_ABGR: reverse rgb with alpha channel first
 * @GST_VIDEO_FORMAT_RGB: rgb
 * @GST_VIDEO_FORMAT_BGR: reverse rgb
 * @GST_VIDEO_FORMAT_Y41B: planar 4:1:1 YUV (Since: 0.10.18)
 * @GST_VIDEO_FORMAT_Y42B: planar 4:2:2 YUV (Since: 0.10.18)
 * @GST_VIDEO_FORMAT_YVYU: packed 4:2:2 YUV (Y0-V0-Y1-U0 Y2-V2-Y3-U2 Y4 ...) (Since: 0.10.23)
 * @GST_VIDEO_FORMAT_Y444: planar 4:4:4 YUV (Since: 0.10.24)
 * @GST_VIDEO_FORMAT_v210: packed 4:2:2 10-bit YUV, complex format (Since: 0.10.24)
 * @GST_VIDEO_FORMAT_v216: packed 4:2:2 16-bit YUV, Y0-U0-Y1-V1 order (Since: 0.10.24)
 * @GST_VIDEO_FORMAT_NV12: planar 4:2:0 YUV with interleaved UV plane (Since: 0.10.26)
 * @GST_VIDEO_FORMAT_NV21: planar 4:2:0 YUV with interleaved VU plane (Since: 0.10.26)
 * @GST_VIDEO_FORMAT_GRAY8: 8-bit grayscale (Since: 0.10.29)
 * @GST_VIDEO_FORMAT_GRAY16_BE: 16-bit grayscale, most significant byte first (Since: 0.10.29)
 * @GST_VIDEO_FORMAT_GRAY16_LE: 16-bit grayscale, least significant byte first (Since: 0.10.29)
 * @GST_VIDEO_FORMAT_v308: packed 4:4:4 YUV (Since: 0.10.29)
 * @GST_VIDEO_FORMAT_Y800: same as GST_VIDEO_FORMAT_GRAY8 (Since: 0.10.30)
 * @GST_VIDEO_FORMAT_Y16: same as GST_VIDEO_FORMAT_GRAY16_LE (Since: 0.10.30)
 * @GST_VIDEO_FORMAT_RGB16: rgb 5-6-5 bits per component (Since: 0.10.30)
 * @GST_VIDEO_FORMAT_BGR16: reverse rgb 5-6-5 bits per component (Since: 0.10.30)
 * @GST_VIDEO_FORMAT_RGB15: rgb 5-5-5 bits per component (Since: 0.10.30)
 * @GST_VIDEO_FORMAT_BGR15: reverse rgb 5-5-5 bits per component (Since: 0.10.30)
 * @GST_VIDEO_FORMAT_UYVP: packed 10-bit 4:2:2 YUV (U0-Y0-V0-Y1 U2-Y2-V2-Y3 U4 ...) (Since: 0.10.31)
 * @GST_VIDEO_FORMAT_A420: planar 4:4:2:0 AYUV (Since: 0.10.31)
 * @GST_VIDEO_FORMAT_RGB8_PALETTED: 8-bit paletted RGB (Since: 0.10.32)
 * @GST_VIDEO_FORMAT_YUV9: planar 4:1:0 YUV (Since: 0.10.32)
 * @GST_VIDEO_FORMAT_YVU9: planar 4:1:0 YUV (like YUV9 but UV planes swapped) (Since: 0.10.32)
 * @GST_VIDEO_FORMAT_IYU1: packed 4:1:1 YUV (Cb-Y0-Y1-Cr-Y2-Y3 ...) (Since: 0.10.32)
 * @GST_VIDEO_FORMAT_ARGB64: rgb with alpha channel first, 16 bits per channel (Since: 0.10.33)
 * @GST_VIDEO_FORMAT_AYUV64: packed 4:4:4 YUV with alpha channel, 16 bits per channel (A0-Y0-U0-V0 ...) (Since: 0.10.33)
 * @GST_VIDEO_FORMAT_r210: packed 4:4:4 RGB, 10 bits per channel (Since: 0.10.33)
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
  GST_VIDEO_FORMAT_Y42B,
  GST_VIDEO_FORMAT_YVYU,
  GST_VIDEO_FORMAT_Y444,
  GST_VIDEO_FORMAT_v210,
  GST_VIDEO_FORMAT_v216,
  GST_VIDEO_FORMAT_NV12,
  GST_VIDEO_FORMAT_NV21,
  GST_VIDEO_FORMAT_GRAY8,
  GST_VIDEO_FORMAT_GRAY16_BE,
  GST_VIDEO_FORMAT_GRAY16_LE,
  GST_VIDEO_FORMAT_v308,
  GST_VIDEO_FORMAT_Y800,
  GST_VIDEO_FORMAT_Y16,
  GST_VIDEO_FORMAT_RGB16,
  GST_VIDEO_FORMAT_BGR16,
  GST_VIDEO_FORMAT_RGB15,
  GST_VIDEO_FORMAT_BGR15,
  GST_VIDEO_FORMAT_UYVP,
  GST_VIDEO_FORMAT_A420,
  GST_VIDEO_FORMAT_RGB8_PALETTED,
  GST_VIDEO_FORMAT_YUV9,
  GST_VIDEO_FORMAT_YVU9,
  GST_VIDEO_FORMAT_IYU1,
  GST_VIDEO_FORMAT_ARGB64,
  GST_VIDEO_FORMAT_AYUV64,
  GST_VIDEO_FORMAT_r210
} GstVideoFormat;

/* format properties */
GstVideoFormat gst_video_format_from_masks           (gint depth, gint bpp, gint endianness,
                                                      gint red_mask, gint green_mask,
                                                      gint blue_mask, gint alpha_mask) G_GNUC_CONST;

GstVideoFormat gst_video_format_from_fourcc          (guint32 fourcc) G_GNUC_CONST;
GstVideoFormat gst_video_format_from_string          (const gchar *format) G_GNUC_CONST;

guint32        gst_video_format_to_fourcc            (GstVideoFormat format) G_GNUC_CONST;
const gchar *  gst_video_format_to_string            (GstVideoFormat format) G_GNUC_CONST;

gboolean       gst_video_format_is_rgb               (GstVideoFormat format) G_GNUC_CONST;
gboolean       gst_video_format_is_yuv               (GstVideoFormat format) G_GNUC_CONST;
gboolean       gst_video_format_is_gray              (GstVideoFormat format) G_GNUC_CONST;
gboolean       gst_video_format_has_alpha            (GstVideoFormat format) G_GNUC_CONST;

int            gst_video_format_get_n_components     (GstVideoFormat format) G_GNUC_CONST;
int            gst_video_format_get_component_depth  (GstVideoFormat format,
                                                      int            component) G_GNUC_CONST;
int            gst_video_format_get_pixel_stride     (GstVideoFormat format,
                                                      int            component) G_GNUC_CONST;

typedef struct _GstVideoInfo GstVideoInfo;
typedef struct _GstVideoFrame GstVideoFrame;

/**
 * GstVideoFlags:
 * @GST_META_VIDEO_FLAG_NONE: no flags
 * @GST_META_VIDEO_FLAG_INTERLACED:
 * @GST_META_VIDEO_FLAG_TTF:
 * @GST_META_VIDEO_FLAG_RFF:
 * @GST_META_VIDEO_FLAG_ONEFIELD:
 * @GST_META_VIDEO_FLAG_TELECINE:
 * @GST_META_VIDEO_FLAG_PROGRESSIVE:
 *
 * Extra video flags
 */
typedef enum {
  GST_VIDEO_FLAG_NONE        = 0,
  GST_VIDEO_FLAG_INTERLACED  = (1 << 0),
  GST_VIDEO_FLAG_TTF         = (1 << 1),
  GST_VIDEO_FLAG_RFF         = (1 << 2),
  GST_VIDEO_FLAG_ONEFIELD    = (1 << 3),
  GST_VIDEO_FLAG_TELECINE    = (1 << 4),
  GST_VIDEO_FLAG_PROGRESSIVE = (1 << 5)
} GstVideoFlags;

#define GST_VIDEO_MAX_PLANES 4

/**
 * GstVideoInfo:
 * @flags: additional video flags
 * @format: the format of the video
 * @width: the width of the video
 * @height: the height of the video
 * @size: the size of one frame
 * @color_matrix: the color matrix.  Possible values are
 *   "sdtv" for the standard definition color matrix (as specified in
 *   Rec. ITU-R BT.470-6) or "hdtv" for the high definition color
 *   matrix (as specified in Rec. ITU-R BT.709)
 * @chroma_site: the chroma siting. Possible values are
 *   "mpeg2" for MPEG-2 style chroma siting (co-sited horizontally,
 *   halfway-sited vertically), "jpeg" for JPEG and Theora style
 *   chroma siting (halfway-sited both horizontally and vertically).
 *   Other chroma site values are possible, but uncommon.
 * @par_n: the pixel-aspect-ratio numerator
 * @par_d: the pixel-aspect-ratio demnominator
 * @fps_n: the framerate numerator
 * @fps_d: the framerate demnominator
 * @n_planes: the number of planes in the image
 * @offset: offsets of the planes
 * @stride: strides of the planes
 *
 * Extra buffer metadata describing image properties
 */
struct _GstVideoInfo {
  GstVideoFormat format;
  GstVideoFlags  flags;
  gint           width;
  gint           height;
  guint          size;

  const gchar   *color_matrix;
  const gchar   *chroma_site;
  GstBuffer     *palette;

  gint           par_n;
  gint           par_d;
  gint           fps_n;
  gint           fps_d;

  guint          n_planes;
  gsize          offset[GST_VIDEO_MAX_PLANES];
  gint           stride[GST_VIDEO_MAX_PLANES];
};

void         gst_video_info_init        (GstVideoInfo *info);

void         gst_video_info_set_format  (GstVideoInfo *info, GstVideoFormat format,
                                         guint width, guint height);

gboolean     gst_video_info_from_caps   (GstVideoInfo *info, const GstCaps  * caps);

GstCaps *    gst_video_info_to_caps     (GstVideoInfo *info);

gboolean     gst_video_info_convert     (GstVideoInfo *info,
                                         GstFormat     src_format,
                                         gint64        src_value,
                                         GstFormat     dest_format,
                                         gint64       *dest_value);

/**
 * GstVideoFrame:
 * @info: the #GstVideoInfo
 * @buffer: the mapped buffer
 * @data: pointers to the plane data
 *
 * A video frame obtained from gst_video_frame_map()
 */
struct _GstVideoFrame {
  GstVideoInfo info;

  GstBuffer *buffer;
  gpointer   meta;

  guint8    *data[GST_VIDEO_MAX_PLANES];
};

#define GST_VIDEO_FRAME_DATA(f,c)    ((f)->data[c])
#define GST_VIDEO_FRAME_STRIDE(f,c)  ((f)->info.stride[c])

gboolean    gst_video_frame_map         (GstVideoFrame *frame, GstVideoInfo *info,
                                         GstBuffer *buffer, GstMapFlags flags);
void        gst_video_frame_unmap       (GstVideoFrame *frame);

#define GST_VIDEO_SIZE_RANGE "(int) [ 1, max ]"
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, max ]"

#define GST_VIDEO_FORMATS_ALL "{ \"I420\"," "\"YV12\"," "\"YUY2\"," "\"UYVY\"," "\"AYUV\"," "\"RGBx\"," \
    "\"BGRx\"," "\"xRGB\"," "\"xBGR\"," "\"RGBA\"," "\"BGRA\"," "\"ARGB\"," "\"ABGR\"," "\"RGB\"," \
    "\"BGR\"," "\"Y41B\"," "\"Y42B\"," "\"YVYU\"," "\"Y444\"," "\"v210\"," "\"v216\"," "\"NV12\"," \
    "\"NV21\"," "\"GRAY8\"," "\"GRAY16_BE\"," "\"GRAY16_LE\"," "\"v308\"," "\"Y800\"," "\"Y16\"," \
    "\"RGB16\"," "\"BGR16\"," "\"RGB15\"," "\"BGR15\"," "\"UYVP\"," "\"A420\"," "\"RGB8_PALETTED\"," \
    "\"YUV9\"," "\"YVU9\"," "\"IYU1\"," "\"ARGB64\"," "\"AYUV64\"," "\"r210\" } "

/**
 * GST_VIDEO_CAPS_MAKE:
 * @format: string format that describes the pixel layout, as string
 *     (e.g. "I420", "RGB", "YV12", "YUY2", "AYUV", etc.)
 *
 * Generic caps string for video, for use in pad templates.
 */
#define GST_VIDEO_CAPS_MAKE(format)                                     \
    "video/x-raw, "                                                     \
    "format = (string) " format ", "                                    \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE

/* buffer flags */

/**
 * GST_VIDEO_BUFFER_TFF:
 *
 * If the #GstBuffer is interlaced, then the first field in the video frame is
 * the top field.  If unset, the bottom field is first.
 *
 * Since: 0.10.23
 */
#define GST_VIDEO_BUFFER_TFF GST_BUFFER_FLAG_MEDIA1

/**
 * GST_VIDEO_BUFFER_RFF:
 *
 * If the #GstBuffer is interlaced, then the first field (as defined by the
 * %GST_VIDEO_BUFFER_TFF flag setting) is repeated.
 *
 * Since: 0.10.23
 */
#define GST_VIDEO_BUFFER_RFF GST_BUFFER_FLAG_MEDIA2

/**
 * GST_VIDEO_BUFFER_ONEFIELD:
 *
 * If the #GstBuffer is interlaced, then only the first field (as defined by the
 * %GST_VIDEO_BUFFER_TFF flag setting) is to be displayed.
 *
 * Since: 0.10.23
 */
#define GST_VIDEO_BUFFER_ONEFIELD GST_BUFFER_FLAG_MEDIA3

/**
 * GST_VIDEO_BUFFER_PROGRESSIVE:
 *
 * If the #GstBuffer is telecined, then the buffer is progressive if the
 * %GST_VIDEO_BUFFER_PROGRESSIVE flag is set, else it is telecine mixed.
 *
 * Since: 0.10.33
 */
#define GST_VIDEO_BUFFER_PROGRESSIVE GST_BUFFER_FLAG_MEDIA4

/* functions */
gboolean       gst_video_calculate_display_ratio (guint * dar_n,
                                                  guint * dar_d,
                                                  guint   video_width,
                                                  guint   video_height,
                                                  guint   video_par_n,
                                                  guint   video_par_d,
                                                  guint   display_par_n,
                                                  guint   display_par_d);

gboolean       gst_video_parse_caps_framerate    (GstCaps * caps, int *fps_n, int *fps_d);
GstBuffer *    gst_video_parse_caps_palette      (GstCaps * caps);

int            gst_video_format_get_component_width  (GstVideoFormat format,
                                                      int            component,
                                                      int            width) G_GNUC_CONST;

int            gst_video_format_get_component_height (GstVideoFormat format,
                                                      int            component,
                                                      int            height) G_GNUC_CONST;
int            gst_video_format_get_component_offset (GstVideoFormat format,
                                                      int            component,
                                                      int            width,
                                                      int            height);

/* video still frame event creation and parsing */

GstEvent *     gst_video_event_new_still_frame   (gboolean in_still);

gboolean       gst_video_event_parse_still_frame (GstEvent * event, gboolean * in_still);


/* convert/encode video frame from one format to another */

typedef void (*GstVideoConvertFrameCallback) (GstBuffer * buf, GError *error, gpointer user_data);

void           gst_video_convert_frame_async (GstBuffer                    * buf,
                                              GstCaps                      * from_caps,
                                              const GstCaps                * to_caps,
                                              GstClockTime                   timeout,
                                              GstVideoConvertFrameCallback   callback,
                                              gpointer                       user_data,
                                              GDestroyNotify                 destroy_notify);

GstBuffer *    gst_video_convert_frame       (GstBuffer     * buf,
                                              GstCaps       * from_caps,
                                              const GstCaps * to_caps,
                                              GstClockTime    timeout,
                                              GError       ** error);
G_END_DECLS

#endif /* __GST_VIDEO_H__ */
