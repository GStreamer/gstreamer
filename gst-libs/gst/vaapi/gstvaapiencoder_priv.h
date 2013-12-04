/*
 *  gstvaapiencoder_priv.h - VA encoder abstraction (private definitions)
 *
 *  Copyright (C) 2013 Intel Corporation
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

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_CAST(encoder) \
    ((GstVaapiEncoder *)(encoder))

#define GST_VAAPI_ENCODER_CLASS(klass) \
    ((GstVaapiEncoderClass *)(klass))

#define GST_VAAPI_ENCODER_GET_CLASS(obj) \
    GST_VAAPI_ENCODER_CLASS(GST_VAAPI_MINI_OBJECT_GET_CLASS(obj))

/* Get GstVaapiDisplay* */
#define GST_VAAPI_ENCODER_DISPLAY(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->display)

/* Get VADisplay */
#define GST_VAAPI_ENCODER_VA_DISPLAY(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->va_display)

/* Get GstVaapiContext* */
#define GST_VAAPI_ENCODER_CONTEXT(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->context)

/* Get VAContext */
#define GST_VAAPI_ENCODER_VA_CONTEXT(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->va_context)

#define GST_VAAPI_ENCODER_VIDEO_INFO(encoder) (GST_VAAPI_ENCODER_CAST(encoder)->video_info)
#define GST_VAAPI_ENCODER_CAPS(encoder)       (GST_VAAPI_ENCODER_CAST(encoder)->caps)
#define GST_VAAPI_ENCODER_WIDTH(encoder)      (GST_VAAPI_ENCODER_CAST(encoder)->video_info.width)
#define GST_VAAPI_ENCODER_HEIGHT(encoder)     (GST_VAAPI_ENCODER_CAST(encoder)->video_info.height)
#define GST_VAAPI_ENCODER_FPS_N(encoder)      (GST_VAAPI_ENCODER_CAST(encoder)->video_info.fps_n)
#define GST_VAAPI_ENCODER_FPS_D(encoder)      (GST_VAAPI_ENCODER_CAST(encoder)->video_info.fps_d)
#define GST_VAAPI_ENCODER_RATE_CONTROL(encoder)   \
    (GST_VAAPI_ENCODER_CAST(encoder)->rate_control)

#define GST_VAAPI_ENCODER_CHECK_STATUS(exp, err_num, err_reason, ...)   \
  if (!(exp)) {                                                         \
    ret = err_num;                                                      \
    GST_VAAPI_ENCODER_LOG_ERROR(err_reason, ## __VA_ARGS__);            \
    goto end;                                                           \
  }

typedef struct _GstVaapiEncoderClass GstVaapiEncoderClass;

struct _GstVaapiEncoder
{
  /*< private > */
  GstVaapiMiniObject parent_instance;

  GstVaapiDisplay *display;
  GstVaapiContext *context;
  GstVaapiContextInfo context_info;
  GstCaps *caps;

  VADisplay va_display;
  VAContextID va_context;
  GstVideoInfo video_info;
  GstVaapiRateControl rate_control;

  GMutex mutex;
  GCond surface_free;
  GCond codedbuf_free;
  guint codedbuf_size;
  GstVaapiVideoPool *codedbuf_pool;
  GAsyncQueue *codedbuf_queue;
};

struct _GstVaapiEncoderClass
{
  GstVaapiMiniObjectClass parent_class;

  gboolean              (*init)         (GstVaapiEncoder * encoder);
  void                  (*finalize)     (GstVaapiEncoder * encoder);

  GstCaps *             (*set_format)   (GstVaapiEncoder * encoder,
                                         GstVideoCodecState * in_state,
                                         GstCaps * ref_caps);

  void                  (*set_context_info) (GstVaapiEncoder * encoder);

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
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, init),                 \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, finalize),             \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, set_format),           \
    GST_VAAPI_ENCODER_CLASS_HOOK (codec, set_context_info),     \
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
