/*
 *  gstvaapiencoder_priv.h - VA encoder abstraction (private definitions)
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_ENCODER_PRIV_H
#define GST_VAAPI_ENCODER_PRIV_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiencoder_objects.h>
#include <gst/vaapi/gstvaapicontext.h>
#include <gst/vaapi/gstvaapivideopool.h>
#include <gst/video/gstvideoutils.h>
#include <gst/vaapi/gstvaapivalue.h>

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_CAST(encoder) \
    ((GstVaapiEncoder *)(encoder))

#define GST_VAAPI_ENCODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_ENCODER, GstVaapiEncoderClass))

#define GST_VAAPI_ENCODER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_ENCODER, GstVaapiEncoderClass))

/**
 * GST_VAAPI_ENCODER_PACKED_HEADERS:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the required set of VA packed headers that
 * need to be submitted along with the corresponding param buffers.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_PACKED_HEADERS
#define GST_VAAPI_ENCODER_PACKED_HEADERS(encoder) \
    GST_VAAPI_ENCODER_CAST(encoder)->packed_headers

/**
 * GST_VAAPI_ENCODER_DISPLAY:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the #GstVaapiDisplay of @encoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_DISPLAY
#define GST_VAAPI_ENCODER_DISPLAY(encoder) \
    GST_VAAPI_ENCODER_CAST(encoder)->display

/**
 * GST_VAAPI_ENCODER_CONTEXT:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the #GstVaapiContext of @encoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_CONTEXT
#define GST_VAAPI_ENCODER_CONTEXT(encoder) \
    GST_VAAPI_ENCODER_CAST(encoder)->context

/**
 * GST_VAAPI_ENCODER_VIDEO_INFO:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the #GstVideoInfo of @encoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_VIDEO_INFO
#define GST_VAAPI_ENCODER_VIDEO_INFO(encoder) \
  (&GST_VAAPI_ENCODER_CAST (encoder)->video_info)

/**
 * GST_VAAPI_ENCODER_WIDTH:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the coded width of the picture.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_WIDTH
#define GST_VAAPI_ENCODER_WIDTH(encoder) \
  (GST_VAAPI_ENCODER_VIDEO_INFO (encoder)->width)

/**
 * GST_VAAPI_ENCODER_HEIGHT:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the coded height of the picture.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_HEIGHT
#define GST_VAAPI_ENCODER_HEIGHT(encoder) \
  (GST_VAAPI_ENCODER_VIDEO_INFO (encoder)->height)

/**
 * GST_VAAPI_ENCODER_FPS_N:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the coded framerate numerator.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_FPS_N
#define GST_VAAPI_ENCODER_FPS_N(encoder) \
  (GST_VAAPI_ENCODER_VIDEO_INFO (encoder)->fps_n)

/**
 * GST_VAAPI_ENCODER_FPS_D:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the coded framerate denominator.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_FPS_D
#define GST_VAAPI_ENCODER_FPS_D(encoder) \
  (GST_VAAPI_ENCODER_VIDEO_INFO (encoder)->fps_d)

/**
 * GST_VAAPI_ENCODER_RATE_CONTROL:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the rate control.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_RATE_CONTROL
#define GST_VAAPI_ENCODER_RATE_CONTROL(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->rate_control)

/**
 * GST_VAAPI_ENCODER_KEYFRAME_PERIOD:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the keyframe period.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_KEYFRAME_PERIOD
#define GST_VAAPI_ENCODER_KEYFRAME_PERIOD(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->keyframe_period)

/**
 * GST_VAAPI_ENCODER_TUNE:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the tuning option.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_TUNE
#define GST_VAAPI_ENCODER_TUNE(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->tune)

/**
 * GST_VAAPI_ENCODER_QUALITY_LEVEL:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to the quality level
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_QUALITY_LEVEL
#define GST_VAAPI_ENCODER_QUALITY_LEVEL(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->va_quality_level.quality_level)

/**
 * GST_VAAPI_ENCODER_VA_RATE_CONTROL:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to #VAEncMiscParameterRateControl
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_VA_RATE_CONTROL
#define GST_VAAPI_ENCODER_VA_RATE_CONTROL(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->va_ratecontrol)

/**
 * GST_VAAPI_ENCODER_VA_FRAME_RATE:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to #VAEncMiscParameterFrameRate
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_VA_FRAME_RATE
#define GST_VAAPI_ENCODER_VA_FRAME_RATE(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->va_framerate)

/**
 * GST_VAAPI_ENCODER_VA_HRD:
 * @encoder: a #GstVaapiEncoder
 *
 * Macro that evaluates to #VAEncMiscParameterHRD
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_ENCODER_VA_HRD
#define GST_VAAPI_ENCODER_VA_HRD(encoder) \
  (GST_VAAPI_ENCODER_CAST (encoder)->va_hrd)

/* Generate a mask for the supplied tuning option (internal) */
#define GST_VAAPI_ENCODER_TUNE_MASK(TUNE) \
  (1U << G_PASTE (GST_VAAPI_ENCODER_TUNE_, TUNE))

#define GST_VAAPI_TYPE_ENCODER_TUNE \
  (gst_vaapi_encoder_tune_get_type ())

#define GST_VAAPI_TYPE_ENCODER_MBBRC \
  (gst_vaapi_encoder_mbbrc_get_type ())

typedef struct _GstVaapiEncoderClass GstVaapiEncoderClass;
typedef struct _GstVaapiEncoderClassData GstVaapiEncoderClassData;

struct _GstVaapiEncoder
{
  /*< private >*/
  GstObject parent_instance;

  GPtrArray *properties;
  GstVaapiDisplay *display;
  GstVaapiContext *context;
  GstVaapiContextInfo context_info;
  GstVaapiEncoderTune tune;
  guint packed_headers;

  VADisplay va_display;
  VAContextID va_context;
  GstVideoInfo video_info;
  GstVaapiProfile profile;
  guint num_ref_frames;
  GstVaapiRateControl rate_control;
  guint32 rate_control_mask;
  guint bitrate; /* kbps */
  guint target_percentage;
  guint keyframe_period;

  /* Maximum number of reference frames supported
   * for the reference picture list 0 and list 2 */
  guint max_num_ref_frames_0;
  guint max_num_ref_frames_1;

  /* parameters */
  VAEncMiscParameterBufferQualityLevel va_quality_level;

  GMutex mutex;
  GCond surface_free;
  GCond codedbuf_free;
  guint codedbuf_size;
  GstVaapiVideoPool *codedbuf_pool;
  GAsyncQueue *codedbuf_queue;
  guint32 num_codedbuf_queued;

  guint got_packed_headers:1;
  guint got_rate_control_mask:1;

  /* miscellaneous buffer parameters */
  VAEncMiscParameterRateControl va_ratecontrol;
  VAEncMiscParameterFrameRate va_framerate;
  VAEncMiscParameterHRD va_hrd;

  gint8 default_roi_value;

  /* trellis quantization */
  gboolean trellis;
};

struct _GstVaapiEncoderClassData
{
  /*< private >*/
  GstVaapiCodec codec;
  guint32 packed_headers;

  GType (*rate_control_get_type)(void);
  GstVaapiRateControl default_rate_control;
  guint32 rate_control_mask;

  GType (*encoder_tune_get_type)(void);
  GstVaapiEncoderTune default_encoder_tune;
  guint32 encoder_tune_mask;
};

#define GST_VAAPI_ENCODER_DEFINE_CLASS_DATA(CODEC)                      \
  GST_VAAPI_TYPE_DEFINE_ENUM_SUBSET_FROM_MASK(                          \
      G_PASTE (GstVaapiRateControl, CODEC),                             \
      G_PASTE (gst_vaapi_rate_control_, CODEC),                         \
      GST_VAAPI_TYPE_RATE_CONTROL, SUPPORTED_RATECONTROLS);             \
                                                                        \
  GST_VAAPI_TYPE_DEFINE_ENUM_SUBSET_FROM_MASK(                          \
      G_PASTE (GstVaapiEncoderTune, CODEC),                             \
      G_PASTE (gst_vaapi_encoder_tune_, CODEC),                         \
      GST_VAAPI_TYPE_ENCODER_TUNE, SUPPORTED_TUNE_OPTIONS);             \
                                                                        \
  static const GstVaapiEncoderClassData g_class_data = {                \
    .codec = G_PASTE (GST_VAAPI_CODEC_, CODEC),                         \
    .packed_headers = SUPPORTED_PACKED_HEADERS,                         \
    .rate_control_get_type =                                            \
        G_PASTE (G_PASTE (gst_vaapi_rate_control_, CODEC), _get_type),  \
    .default_rate_control = DEFAULT_RATECONTROL,                        \
    .rate_control_mask = SUPPORTED_RATECONTROLS,                        \
    .encoder_tune_get_type =                                            \
        G_PASTE (G_PASTE (gst_vaapi_encoder_tune_, CODEC), _get_type),  \
    .default_encoder_tune = GST_VAAPI_ENCODER_TUNE_NONE,                \
    .encoder_tune_mask = SUPPORTED_TUNE_OPTIONS,                        \
  }

struct _GstVaapiEncoderClass
{
  /*< private >*/
  GstObjectClass parent_class;

  const GstVaapiEncoderClassData *class_data;

  GstVaapiEncoderStatus (*reconfigure)  (GstVaapiEncoder * encoder);
  GstVaapiEncoderStatus (*reordering)   (GstVaapiEncoder * encoder,
                                         GstVideoCodecFrame * in,
                                         GstVaapiEncPicture ** out);
  GstVaapiEncoderStatus (*encode)       (GstVaapiEncoder * encoder,
                                         GstVaapiEncPicture * picture,
                                         GstVaapiCodedBufferProxy * codedbuf);

  GstVaapiEncoderStatus (*flush)        (GstVaapiEncoder * encoder);

  /* get_codec_data can be NULL */
  GstVaapiEncoderStatus (*get_codec_data) (GstVaapiEncoder * encoder,
                                           GstBuffer ** codec_data);

  /* Iterator that retrieves the pending pictures in the reordered
   * list */
  gboolean              (*get_pending_reordered) (GstVaapiEncoder * encoder,
                                                  GstVaapiEncPicture ** picture,
                                                  gpointer * state);
};

G_GNUC_INTERNAL
GstVaapiSurfaceProxy *
gst_vaapi_encoder_create_surface (GstVaapiEncoder *
    encoder);

static inline void
gst_vaapi_encoder_release_surface (GstVaapiEncoder * encoder,
    GstVaapiSurfaceProxy * proxy)
{
  gst_vaapi_surface_proxy_unref (proxy);
}

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_param_quality_level (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_param_control_rate (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_param_roi_regions (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_param_trellis (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_num_slices (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint,
    guint media_max_slices, guint * num_slices);

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_max_num_ref_frames (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint);

G_GNUC_INTERNAL
gboolean
gst_vaapi_encoder_ensure_tile_support (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint);

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_PRIV_H */
