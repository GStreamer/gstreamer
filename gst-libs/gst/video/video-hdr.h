/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_VIDEO_HDR_H__
#define __GST_VIDEO_HDR_H__

#include <gst/gst.h>
#include <gst/video/video-prelude.h>

G_BEGIN_DECLS

typedef struct _GstVideoMasteringDisplayInfo GstVideoMasteringDisplayInfo;
typedef struct _GstVideoContentLightLevel GstVideoContentLightLevel;

/**
 * GstVideoMasteringDisplayInfo:
 * @Rx_n: the numerator of normalized red x coordinate as defined CIE 1931
 * @Rx_d: the denominator of normalized red x coordinate as defined CIE 1931
 * @Ry_n: the numerator of normalized red y coordinate as defined CIE 1931
 * @Ry_d: the denominator of normalized red y coordinate as defined CIE 1931
 * @Gx_n: the numerator of normalized green x coordinate as defined CIE 1931
 * @Gx_d: the denominator of normalized green x coordinate as defined CIE 1931
 * @Gy_n: the numerator of normalized green y coordinate as defined CIE 1931
 * @Gy_d: the denominator of normalized green y coordinate as defined CIE 1931
 * @Bx_n: the numerator of normalized blue x coordinate as defined CIE 1931
 * @Bx_d: the denominator of normalized blue x coordinate as defined CIE 1931
 * @By_n: the numerator of normalized blue y coordinate as defined CIE 1931
 * @By_d: the denominator of normalized blue y coordinate as defined CIE 1931
 * @Wx_n: the numerator of normalized white x coordinate as defined CIE 1931
 * @Wx_d: the denominator of normalized white x coordinate as defined CIE 1931
 * @Wy_n: the numerator of normalized white y coordinate as defined CIE 1931
 * @Wy_d: the denominator of normalized white y coordinate as defined CIE 1931
 * @max_luma_n: the numerator of maximum display luminance in candelas per square meter (cd/m^2 and nit)
 * @max_luma_d: the denominator of maximum display luminance in candelas per square meter (cd/m^2 and nit)
 * @min_luma_n: the numerator of minimum display luminance in candelas per square meter (cd/m^2 and nit)
 * @min_luma_d: the denominator of minimum display luminance in candelas per square meter (cd/m^2 and nit)
 *
 * Mastering display color volume information defined by SMPTE ST 2086
 * (a.k.a static HDR metadata).
 * Each pair of *_d and *_n represents fraction value of red, green, blue, white
 * and min/max luma.
 *
 * The decimal representation of each red, green, blue and white value should
 * be in the range of [0, 1].
 *
 * Since: 1.18
 */
struct _GstVideoMasteringDisplayInfo
{
  guint Rx_n, Rx_d, Ry_n, Ry_d;
  guint Gx_n, Gx_d, Gy_n, Gy_d;
  guint Bx_n, Bx_d, By_n, By_d;
  guint Wx_n, Wx_d, Wy_n, Wy_d;
  guint max_luma_n, max_luma_d;
  guint min_luma_n, min_luma_d;

  /*< private >*/
  guint _gst_reserved[GST_PADDING];
};

GST_VIDEO_API
void      gst_video_mastering_display_info_init         (GstVideoMasteringDisplayInfo * minfo);

GST_VIDEO_API
gboolean  gst_video_mastering_display_info_from_string  (GstVideoMasteringDisplayInfo * minfo,
                                                         const gchar * mastering);

GST_VIDEO_API
gchar *   gst_video_mastering_display_info_to_string    (const GstVideoMasteringDisplayInfo * minfo);

GST_VIDEO_API
gboolean  gst_video_mastering_display_info_is_equal     (const GstVideoMasteringDisplayInfo * minfo,
                                                         const GstVideoMasteringDisplayInfo * other);

GST_VIDEO_API
gboolean  gst_video_mastering_display_info_is_valid     (const GstVideoMasteringDisplayInfo * minfo);

GST_VIDEO_API
gboolean  gst_video_mastering_display_info_from_caps    (GstVideoMasteringDisplayInfo * minfo,
                                                         const GstCaps * caps);

GST_VIDEO_API
gboolean  gst_video_mastering_display_info_add_to_caps  (const GstVideoMasteringDisplayInfo * minfo,
                                                         GstCaps * caps);

/**
 * GstVideoContentLightLevel:
 * @maxCLL_n: the numerator of Maximum Content Light Level (cd/m^2 and nit)
 * @maxCLL_d: the denominator of Maximum Content Light Level (cd/m^2 and nit)
 * @maxFALL_n: the numerator Maximum Frame-Average Light Level (cd/m^2 and nit)
 * @maxFALL_d: the denominator Maximum Frame-Average Light Level (cd/m^2 and nit)
 *
 * Content light level information specified in CEA-861.3, Appendix A.
 *
 * Since: 1.18
 */
struct _GstVideoContentLightLevel
{
  guint maxCLL_n, maxCLL_d;
  guint maxFALL_n, maxFALL_d;

  /*< private >*/
  guint _gst_reserved[GST_PADDING];
};

GST_VIDEO_API
void      gst_video_content_light_level_init         (GstVideoContentLightLevel * linfo);

GST_VIDEO_API
gboolean  gst_video_content_light_level_from_string  (GstVideoContentLightLevel * linfo,
                                                      const gchar * level);

GST_VIDEO_API
gchar *   gst_video_content_light_level_to_string    (const GstVideoContentLightLevel * linfo);

GST_VIDEO_API
gboolean  gst_video_content_light_level_from_caps    (GstVideoContentLightLevel * linfo,
                                                      const GstCaps * caps);

GST_VIDEO_API
gboolean  gst_video_content_light_level_add_to_caps  (const GstVideoContentLightLevel * linfo,
                                                      GstCaps * caps);


G_END_DECLS

#endif /* __GST_VIDEO_HDR_H__ */
