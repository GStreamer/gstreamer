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
#include <gst/video/video-info.h>

G_BEGIN_DECLS

/**
 * GstVideoHDRFormat:
 * @GST_VIDEO_HDR_FORMAT_NONE: No HDR format detected.
 * @GST_VIDEO_HDR_FORMAT_HDR10: HDR10 format
 * @GST_VIDEO_HDR_FORMAT_HDR10_PLUS: HDR10+ format
 * * @GST_VIDEO_HDR_FORMAT_DOLBY_VISION: Dolby Vision format
 *
 * Enum value describing the most common video for High Dynamic Range (HDR) formats.
 *
 * Since: 1.20
 */
typedef enum {
  GST_VIDEO_HDR_FORMAT_NONE,
  GST_VIDEO_HDR_FORMAT_HDR10,
  GST_VIDEO_HDR_FORMAT_HDR10_PLUS,
  GST_VIDEO_HDR_FORMAT_DOLBY_VISION,
} GstVideoHDRFormat;

#define GST_VIDEO_HDR10_PLUS_MAX_BYTES 1024
/* defined in CTA-861-G */
#define GST_VIDEO_HDR10_PLUS_NUM_WINDOWS 1 /* number of windows, shall be 1. */
#define GST_VIDEO_HDR10_PLUS_MAX_TSD_APL 25 /* targeted_system_display_actual_peak_luminance max value */
#define GST_VIDEO_HDR10_PLUS_MAX_MD_APL 25 /* mastering_display_actual_peak_luminance max value */

typedef struct _GstVideoMasteringDisplayInfoCoordinates GstVideoMasteringDisplayInfoCoordinates;
typedef struct _GstVideoMasteringDisplayInfo GstVideoMasteringDisplayInfo;
typedef struct _GstVideoContentLightLevel GstVideoContentLightLevel;
typedef struct _GstVideoHDR10Plus GstVideoHDR10Plus;
typedef struct _GstVideoColorVolumeTransformation GstVideoColorVolumeTransformation;

/**
 * GstVideoHDRMeta:
 * @meta: parent #GstMeta
 * @format: The type of dynamic HDR contained in the meta.
 * @data: contains the dynamic HDR data
 * @size: The size in bytes of @data
 *
 * Dynamic HDR data should be included in video user data
 *
 * Since: 1.20
 */
typedef struct {
  GstMeta meta;
  GstVideoHDRFormat format;
  guint8 *data;
  gsize size;
} GstVideoHDRMeta;

GST_VIDEO_API
GType gst_video_hdr_meta_api_get_type (void);
#define GST_VIDEO_HDR_META_API_TYPE (gst_video_hdr_meta_api_get_type())

GST_VIDEO_API
const GstMetaInfo *gst_video_hdr_meta_get_info (void);
#define GST_VIDEO_HDR_META_INFO (gst_video_hdr_meta_get_info())

/**
 * gst_buffer_get_video_hdr_meta:
 * @b: A #GstBuffer
 *
 * Gets the #GstVideoHDRMeta that might be present on @b.
 *
 * Since: 1.20
 *
 * Returns: The first #GstVideoHDRMeta present on @b, or %NULL if
 * no #GstVideoHDRMeta are present
 */
#define gst_buffer_get_video_hdr_meta(b) \
        ((GstVideoHDRMeta*)gst_buffer_get_meta((b), GST_VIDEO_HDR_META_API_TYPE))

GST_VIDEO_API
GstVideoHDRMeta *gst_buffer_add_video_hdr_meta (GstBuffer * buffer, GstVideoHDRFormat format,
                                                const guint8 * data, gsize size);

/**
 * GstVideoMasteringDisplayInfoCoordinates:
 * @x: the x coordinate of CIE 1931 color space in unit of 0.00002.
 * @y: the y coordinate of CIE 1931 color space in unit of 0.00002.
 *
 * Used to represent display_primaries and white_point of
 * #GstVideoMasteringDisplayInfo struct. See #GstVideoMasteringDisplayInfo
 *
 * Since: 1.18
 */
struct _GstVideoMasteringDisplayInfoCoordinates
{
  guint16 x;
  guint16 y;
};

/**
 * GstVideoMasteringDisplayInfo:
 * @display_primaries: the xy coordinates of primaries in the CIE 1931 color space.
 *   the index 0 contains red, 1 is for green and 2 is for blue.
 *   each value is normalized to 50000 (meaning that in unit of 0.00002)
 * @white_point: the xy coordinates of white point in the CIE 1931 color space.
 *   each value is normalized to 50000 (meaning that in unit of 0.00002)
 * @max_display_mastering_luminance: the maximum value of display luminance
 *   in unit of 0.0001 candelas per square metre (cd/m^2 and nit)
 * @min_display_mastering_luminance: the minimum value of display luminance
 *   in unit of 0.0001 candelas per square metre (cd/m^2 and nit)
 *
 * Mastering display color volume information defined by SMPTE ST 2086
 * (a.k.a static HDR metadata).
 *
 * Since: 1.18
 */
struct _GstVideoMasteringDisplayInfo
{
  GstVideoMasteringDisplayInfoCoordinates display_primaries[3];
  GstVideoMasteringDisplayInfoCoordinates white_point;
  guint32 max_display_mastering_luminance;
  guint32 min_display_mastering_luminance;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
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
gboolean  gst_video_mastering_display_info_from_caps    (GstVideoMasteringDisplayInfo * minfo,
                                                         const GstCaps * caps);

GST_VIDEO_API
gboolean  gst_video_mastering_display_info_add_to_caps  (const GstVideoMasteringDisplayInfo * minfo,
                                                         GstCaps * caps);

/**
 * GstVideoContentLightLevel:
 * @max_content_light_level: the maximum content light level
 *   (abbreviated to MaxCLL) in candelas per square meter (cd/m^2 and nit)
 * @max_frame_average_light_level: the maximum frame average light level
 *   (abbreviated to MaxFLL) in candelas per square meter (cd/m^2 and nit)
 *
 * Content light level information specified in CEA-861.3, Appendix A.
 *
 * Since: 1.18
 */
struct _GstVideoContentLightLevel
{
  guint16 max_content_light_level;
  guint16 max_frame_average_light_level;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
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

/**
 * GstVideoColorVolumeTransformation:
 * @window_upper_left_corner_x: the x coordinate of the top left pixel of the w-th processing
 * @window_upper_left_corner_y: the y coordinate of the top left pixel of the w-th processing
 * @window_lower_right_corner_x: the x coordinate of the lower right pixel of the w-th processing
 * @window_lower_right_corner_y: the y coordinate of the lower right pixel of the w-th processing
 * @center_of_ellipse_x: the x coordinate of the center position of the concentric internal
 * and external ellipses of the elliptical pixel selector in the w-th processing window
 * @center_of_ellipse_y: the y coordinate of the center position of the concentric internal
 * and external ellipses of the elliptical pixel selector in the w-th processing window
 * @rotation_angle: the clockwise rotation angle in degree of arc with respect to the
 * positive direction of the x-axis of the concentric internal and external ellipses of the elliptical
 * pixel selector in the w-th processing window
 * @semimajor_axis_internal_ellipse: the semi-major axis value of the internal ellipse of the
 * elliptical pixel selector in amount of pixels in the w-th processing window
 * @semimajor_axis_external_ellipse: the semi-major axis value of the external ellipse of
 * the elliptical pixel selector in amount of pixels in the w-th processing window
 * @semiminor_axis_external_ellipse: the semi-minor axis value of the external ellipse of
 * the elliptical pixel selector in amount of pixels in the w-th processing window
 * @overlap_process_option: one of the two methods of combining
 * rendered pixels in the w-th processing window in an image with at least one elliptical pixel
 * selector
 * @maxscl: the maximum of the i-th color component of linearized RGB values in the
 * w-th processing window in the scene
 * @average_maxrgb: the average of linearized maxRGB values in the w-th processing
 * window in the scene
 * @num_distribution_maxrgb_percentiles: the number of linearized maxRGB values at
 * given percentiles in the w-th processing window in the scene. Maximum value should be 9.
 * @distribution_maxrgb_percentages: an integer percentage value corresponding to the
 * i-th percentile linearized RGB value in the w-th processing window in the scene
 * @fraction_bright_pixels: the fraction of selected pixels in the image that contains the
 * brightest pixel in the scene
 * @tone_mapping_flag: true if the tone mapping function in the w-th
 * processing window is present
 * @knee_point_x: the x coordinate of the separation point between the linear part and the
 * curved part of the tone mapping function
 * @knee_point_y: the y coordinate of the separation point between the linear part and the
 * curved part of the tone mapping function
 * @num_bezier_curve_anchors: the number of the intermediate anchor parameters of the
 * tone mapping function in the w-th processing window. Maximum value should be 9.
 * @bezier_curve_anchors: the i-th intermediate anchor parameter of the tone mapping
function in the w-th processing window in the scene
 * @color_saturation_mapping_flag: shall be equal to zero in this version of the standard.
 * @color_saturation_weight: a number that shall adjust the color saturation gain in the w-
th processing window in the scene
 *
 * Processing window in dynamic metadata defined in SMPTE ST 2094-40:2016
 * and CTA-861-G Annex S HDR Dynamic Metadata Syntax Type 4.
 *
 * Since: 1.20
 */
struct _GstVideoColorVolumeTransformation
{
  guint16 window_upper_left_corner_x;
  guint16 window_upper_left_corner_y;
  guint16 window_lower_right_corner_x;
  guint16 window_lower_right_corner_y;
  guint16 center_of_ellipse_x;
  guint16 center_of_ellipse_y;
  guint8  rotation_angle;
  guint16 semimajor_axis_internal_ellipse;
  guint16 semimajor_axis_external_ellipse;
  guint16 semiminor_axis_external_ellipse;
  guint8 overlap_process_option;
  guint32 maxscl[3];
  guint32 average_maxrgb;
  guint8 num_distribution_maxrgb_percentiles;
  guint8 distribution_maxrgb_percentages[16];
  guint32 distribution_maxrgb_percentiles[16];
  guint16 fraction_bright_pixels;
  guint8 tone_mapping_flag;
  guint16 knee_point_x;
  guint16 knee_point_y;
  guint8 num_bezier_curve_anchors;
  guint16 bezier_curve_anchors[16];
  guint8 color_saturation_mapping_flag;
  guint8 color_saturation_weight;

  /*< private >*/
  guint32 _gst_reserved[GST_PADDING];
};

/**
 * GstVideoHDR10Plus:
 * @application_identifier: the application identifier
 * @application_version: the application version
 * @num_windows: the number of processing windows. The first processing window shall be
 * for the entire picture
 * @processing_window: the color volume transformation for the processing window.
 * @targeted_system_display_maximum_luminance: the nominal maximum display luminance
 * of the targeted system display in units of 0.0001 candelas per square meter
 * @targeted_system_display_actual_peak_luminance_flag: shall be equal to zero in this
 * version of the standard
 * @num_rows_targeted_system_display_actual_peak_luminance: the number of rows
 * in the targeted_system_display_actual_peak_luminance array
 * @num_cols_targeted_system_display_actual_peak_luminance: the number of columns in the
 * targeted_system_display_actual_peak_luminance array
 * @targeted_system_display_actual_peak_luminance: the normalized actual peak luminance of
 * the targeted system display
 * @mastering_display_actual_peak_luminance_flag: shall be equal to 0 for this version of this Standard
 * @num_rows_mastering_display_actual_peak_luminance: the number of rows in the
 * mastering_display_actual_peak_luminance array
 * @num_cols_mastering_display_actual_peak_luminance: the number of columns in the
 * mastering_display_actual_peak_luminance array.
 * @mastering_display_actual_peak_luminance: the normalized actual peak luminance of
 * the mastering display used for mastering the image essence
 *
 * Dynamic HDR 10+ metadata defined in SMPTE2094-40
 * and CTA-861-G Annex S HDR Dynamic Metadata Syntax Type 4.
 *
 * Since: 1.20
 */
struct _GstVideoHDR10Plus
{
  guint8 application_identifier;
  guint8 application_version;
  guint8 num_windows;
  GstVideoColorVolumeTransformation processing_window[GST_VIDEO_HDR10_PLUS_NUM_WINDOWS];
  guint32 targeted_system_display_maximum_luminance;
  guint8 targeted_system_display_actual_peak_luminance_flag;
  guint8 num_rows_targeted_system_display_actual_peak_luminance;
  guint8 num_cols_targeted_system_display_actual_peak_luminance;
  guint8 targeted_system_display_actual_peak_luminance[GST_VIDEO_HDR10_PLUS_MAX_TSD_APL][GST_VIDEO_HDR10_PLUS_MAX_TSD_APL];
  guint8 mastering_display_actual_peak_luminance_flag;
  guint8 num_rows_mastering_display_actual_peak_luminance;
  guint8 num_cols_mastering_display_actual_peak_luminance;
  guint8 mastering_display_actual_peak_luminance[GST_VIDEO_HDR10_PLUS_MAX_MD_APL][GST_VIDEO_HDR10_PLUS_MAX_MD_APL];

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_VIDEO_API gboolean
gst_video_hdr_parse_hdr10_plus (const guint8 * data, gsize size,
                                GstVideoHDR10Plus * hdr10_plus);

G_END_DECLS

#endif /* __GST_VIDEO_HDR_H__ */
