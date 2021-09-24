/*
 *  gstvaapipostproc.h - VA-API video post processing
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
*/

#ifndef GST_VAAPIPOSTPROC_H
#define GST_VAAPIPOSTPROC_H

#include "gstvaapipluginbase.h"
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapifilter.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIPOSTPROC \
  (gst_vaapipostproc_get_type ())
#define GST_VAAPIPOSTPROC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPIPOSTPROC, GstVaapiPostproc))
#define GST_VAAPIPOSTPROC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPIPOSTPROC, \
       GstVaapiPostprocClass))
#define GST_IS_VAAPIPOSTPROC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPIPOSTPROC))
#define GST_IS_VAAPIPOSTPROC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPIPOSTPROC))
#define GST_VAAPIPOSTPROC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_VAAPIPOSTPROC, \
       GstVaapiPostprocClass))

typedef struct _GstVaapiPostproc GstVaapiPostproc;
typedef struct _GstVaapiPostprocClass GstVaapiPostprocClass;
typedef struct _GstVaapiDeinterlaceState GstVaapiDeinterlaceState;

/**
 * GstVaapiHDRToneMap:
 * @GST_VAAPI_TYPE_HDR_TONE_MAP_AUTO: Auto detect need for hdr tone map.
 * @GST_VAAPI_TYPE_HDR_TONE_MAP_DISABLED: Never perform hdr tone map.
 */
typedef enum
{
  GST_VAAPI_HDR_TONE_MAP_AUTO = 0,
  GST_VAAPI_HDR_TONE_MAP_DISABLED,
} GstVaapiHDRToneMap;

/**
 * GstVaapiDeinterlaceMode:
 * @GST_VAAPI_DEINTERLACE_MODE_AUTO: Auto detect needs for deinterlacing.
 * @GST_VAAPI_DEINTERLACE_MODE_INTERLACED: Force deinterlacing.
 * @GST_VAAPI_DEINTERLACE_MODE_DISABLED: Never perform deinterlacing.
 */
typedef enum
{
  GST_VAAPI_DEINTERLACE_MODE_AUTO = 0,
  GST_VAAPI_DEINTERLACE_MODE_INTERLACED,
  GST_VAAPI_DEINTERLACE_MODE_DISABLED,
} GstVaapiDeinterlaceMode;

/*
 * GST_VAAPI_DEINTERLACE_MAX_REFERENCES:
 *
 * This represents the maximum number of VA surfaces we could keep as
 * references for advanced deinterlacing.
 *
 * Note: if the upstream element is vaapidecode, then the maximum
 * number of allowed surfaces used as references shall be less than
 * the actual number of scratch surfaces used for decoding (4).
 */
#define GST_VAAPI_DEINTERLACE_MAX_REFERENCES 2

/**
 * GstVaapiPostprocFlags:
 * @GST_VAAPI_POSTPROC_FLAG_FORMAT: Pixel format conversion.
 * @GST_VAAPI_POSTPROC_FLAG_DENOISE: Noise reduction.
 * @GST_VAAPI_POSTPROC_FLAG_SHARPEN: Sharpening.
 * @GST_VAAPI_POSTPROC_FLAG_HUE: Change color hue.
 * @GST_VAAPI_POSTPROC_FLAG_SATURATION: Change saturation.
 * @GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS: Change brightness.
 * @GST_VAAPI_POSTPROC_FLAG_CONTRAST: Change contrast.
 * @GST_VAAPI_POSTPROC_FLAG_DEINTERLACE: Deinterlacing.
 * @GST_VAAPI_POSTPROC_FLAG_SIZE: Video scaling.
 * @GST_VAAPI_POSTPROC_FLAG_SCALE: Video scaling mode.
 * @GST_VAAPI_POSTPROC_FLAG_VIDEO_DIRECTION: Video rotation and flip/mirroring.
 * @GST_VAAPI_POSTPROC_FLAG_HDR_TONE_MAP: HDR tone mapping.
 * @GST_VAAPI_POSTPROC_FLAG_SKINTONE: Skin tone enhancement.
 * @GST_VAAPI_POSTPROC_FLAG_SKINTONE_LEVEL: Skin tone enhancement with value.
 *
 * The set of operations that are to be performed for each frame.
 */
typedef enum
{
  GST_VAAPI_POSTPROC_FLAG_FORMAT      = 1 << GST_VAAPI_FILTER_OP_FORMAT,
  GST_VAAPI_POSTPROC_FLAG_DENOISE     = 1 << GST_VAAPI_FILTER_OP_DENOISE,
  GST_VAAPI_POSTPROC_FLAG_SHARPEN     = 1 << GST_VAAPI_FILTER_OP_SHARPEN,
  GST_VAAPI_POSTPROC_FLAG_HUE         = 1 << GST_VAAPI_FILTER_OP_HUE,
  GST_VAAPI_POSTPROC_FLAG_SATURATION  = 1 << GST_VAAPI_FILTER_OP_SATURATION,
  GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS  = 1 << GST_VAAPI_FILTER_OP_BRIGHTNESS,
  GST_VAAPI_POSTPROC_FLAG_CONTRAST    = 1 << GST_VAAPI_FILTER_OP_CONTRAST,
  GST_VAAPI_POSTPROC_FLAG_DEINTERLACE = 1 << GST_VAAPI_FILTER_OP_DEINTERLACING,
  GST_VAAPI_POSTPROC_FLAG_SCALE       = 1 << GST_VAAPI_FILTER_OP_SCALING,
  GST_VAAPI_POSTPROC_FLAG_VIDEO_DIRECTION =
      1 << GST_VAAPI_FILTER_OP_VIDEO_DIRECTION,
  GST_VAAPI_POSTPROC_FLAG_CROP        = 1 << GST_VAAPI_FILTER_OP_CROP,
  GST_VAAPI_POSTPROC_FLAG_HDR_TONE_MAP = 1 << GST_VAAPI_FILTER_OP_HDR_TONE_MAP,
#ifndef GST_REMOVE_DEPRECATED
  GST_VAAPI_POSTPROC_FLAG_SKINTONE    = 1 << GST_VAAPI_FILTER_OP_SKINTONE,
#endif
  GST_VAAPI_POSTPROC_FLAG_SKINTONE_LEVEL =
      1 << GST_VAAPI_FILTER_OP_SKINTONE_LEVEL,

  /* Additional custom flags */
  GST_VAAPI_POSTPROC_FLAG_CUSTOM      = 1 << 20,
  GST_VAAPI_POSTPROC_FLAG_SIZE        = GST_VAAPI_POSTPROC_FLAG_CUSTOM,
} GstVaapiPostprocFlags;

/*
 * GstVaapiDeinterlaceState:
 * @buffers: history buffer, maintained as a cyclic array
 * @buffers_index: next free slot in the history buffer
 * @surfaces: array of surfaces used as references
 * @num_surfaces: number of active surfaces in that array
 * @deint: flag: previous buffers were interlaced?
 * @tff: flag: previous buffers were organized as top-field-first?
 *
 * Context used to maintain deinterlacing state.
 */
struct _GstVaapiDeinterlaceState
{
  GstBuffer *buffers[GST_VAAPI_DEINTERLACE_MAX_REFERENCES];
  guint buffers_index;
  GstVaapiSurface *surfaces[GST_VAAPI_DEINTERLACE_MAX_REFERENCES];
  guint num_surfaces;
  guint deint:1;
  guint tff:1;
};

struct _GstVaapiPostproc
{
  /*< private >*/
  GstVaapiPluginBase parent_instance;

  GMutex postproc_lock;
  GstVaapiFilter *filter;
  GPtrArray *filter_ops;
  GstVaapiVideoPool *filter_pool;
  GstVideoInfo filter_pool_info;
  GArray *filter_formats;
  GstVideoFormat format;        /* output video format (encoded) */
  guint width;
  guint height;
  guint flags;

  GstCaps *allowed_sinkpad_caps;
  GstVideoInfo sinkpad_info;
  GstCaps *allowed_srcpad_caps;
  GstVideoInfo srcpad_info;

  /* HDR Tone Mapping */
  GstVaapiHDRToneMap hdr_tone_map;

  /* Deinterlacing */
  GstVaapiDeinterlaceMode deinterlace_mode;
  GstVaapiDeinterlaceMethod deinterlace_method;
  GstVaapiDeinterlaceState deinterlace_state;
  GstClockTime field_duration;

  /* Basic filter values */
  gfloat denoise_level;
  gfloat sharpen_level;

  GstVaapiScaleMethod scale_method;

  GstVideoOrientationMethod video_direction;
  GstVideoOrientationMethod tag_video_direction;

  /* Cropping */
  guint crop_left;
  guint crop_right;
  guint crop_top;
  guint crop_bottom;

  /* Color balance filter values */
  gfloat hue;
  gfloat saturation;
  gfloat brightness;
  gfloat contrast;

  gboolean skintone_enhance;
  guint skintone_value;
  gboolean forward_crop;

  guint get_va_surfaces:1;
  guint has_vpp:1;
  guint use_vpp:1;
  guint keep_aspect:1;

  /* color balance's channel list */
  GList *cb_channels;
  gboolean same_caps;
};

struct _GstVaapiPostprocClass
{
  /*< private >*/
  GstVaapiPluginBaseClass parent_class;
};

GType
gst_vaapipostproc_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPIPOSTPROC_H */
