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
    ((GstVaapiEncoderClass *)(klass))

#define GST_VAAPI_ENCODER_GET_CLASS(obj) \
    GST_VAAPI_ENCODER_CLASS(GST_VAAPI_MINI_OBJECT_GET_CLASS(obj))

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

/* Generate a mask for the supplied tuning option (internal) */
#define GST_VAAPI_ENCODER_TUNE_MASK(TUNE) \
  (1U << G_PASTE (GST_VAAPI_ENCODER_TUNE_, TUNE))

#define GST_VAAPI_TYPE_ENCODER_TUNE \
  (gst_vaapi_encoder_tune_get_type ())

typedef struct _GstVaapiEncoderClass GstVaapiEncoderClass;
typedef struct _GstVaapiEncoderClassData GstVaapiEncoderClassData;

/* Private GstVaapiEncoderPropInfo definition */
typedef struct {
  gint prop;
  GParamSpec *pspec;
} GstVaapiEncoderPropData;

#define GST_VAAPI_ENCODER_PROPERTIES_APPEND(props, id, pspec) do {      \
    props = gst_vaapi_encoder_properties_append (props, id, pspec);     \
    if (!props)                                                         \
      return NULL;                                                      \
  } while (0)

G_GNUC_INTERNAL
GPtrArray *
gst_vaapi_encoder_properties_append (GPtrArray * props, gint prop_id,
    GParamSpec *pspec);

G_GNUC_INTERNAL
GPtrArray *
gst_vaapi_encoder_properties_get_default (const GstVaapiEncoderClass * klass);

struct _GstVaapiEncoder
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;

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
  guint keyframe_period;

  GMutex mutex;
  GCond surface_free;
  GCond codedbuf_free;
  guint codedbuf_size;
  GstVaapiVideoPool *codedbuf_pool;
  GAsyncQueue *codedbuf_queue;
  guint32 num_codedbuf_queued;

  guint got_packed_headers:1;
  guint got_rate_control_mask:1;
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
  GstVaapiMiniObjectClass parent_class;

  const GstVaapiEncoderClassData *class_data;

  gboolean              (*init)         (GstVaapiEncoder * encoder);
  void                  (*finalize)     (GstVaapiEncoder * encoder);

  GstVaapiEncoderStatus (*reconfigure)  (GstVaapiEncoder * encoder);

  GPtrArray *           (*get_default_properties) (void);
  GstVaapiEncoderStatus (*set_property) (GstVaapiEncoder * encoder,
                                         gint prop_id,
                                         const GValue * value);

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
};

#define GST_VAAPI_ENCODER_CLASS_HOOK(codec, func) \
  .func = G_PASTE (G_PASTE (G_PASTE (gst_vaapi_encoder_,codec),_), func)

#define GST_VAAPI_ENCODER_CLASS_INIT_BASE(CODEC)                \
  .parent_class = {                                             \
    .size = sizeof (G_PASTE (GstVaapiEncoder, CODEC)),          \
    .finalize = (GDestroyNotify) gst_vaapi_encoder_finalize     \
  }

#define GST_VAAPI_ENCODER_CLASS_INIT(CODEC, codec)              \
  GST_VAAPI_ENCODER_CLASS_INIT_BASE (CODEC),                    \
    .class_data = &g_class_data,                                \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, init),                 \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, finalize),             \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, reconfigure),          \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, get_default_properties), \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, reordering),           \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, encode),               \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, flush)

G_GNUC_INTERNAL
GstVaapiEncoder *
gst_vaapi_encoder_new (const GstVaapiEncoderClass * klass,
    GstVaapiDisplay * display);

G_GNUC_INTERNAL
void
gst_vaapi_encoder_finalize (GstVaapiEncoder * encoder);

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

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_PRIV_H */
