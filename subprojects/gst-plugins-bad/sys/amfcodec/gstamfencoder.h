/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstamfutils.h"

G_BEGIN_DECLS

#define GST_TYPE_AMF_ENCODER            (gst_amf_encoder_get_type())
#define GST_AMF_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMF_ENCODER, GstAmfEncoder))
#define GST_AMF_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMF_ENCODER, GstAmfEncoderClass))
#define GST_IS_AMF_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMF_ENCODER))
#define GST_IS_AMF_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMF_ENCODER))
#define GST_AMF_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_AMF_ENCODER, GstAmfEncoderClass))
#define GST_AMF_ENCODER_CAST(obj)       ((GstAmfEncoder *)obj)

typedef struct _GstAmfEncoder GstAmfEncoder;
typedef struct _GstAmfEncoderClass GstAmfEncoderClass;
typedef struct _GstAmfEncoderPrivate GstAmfEncoderPrivate;

struct _GstAmfEncoder
{
  GstVideoEncoder parent;

  GstAmfEncoderPrivate *priv;
};

struct _GstAmfEncoderClass
{
  GstVideoEncoderClass parent_class;

  gboolean    (*set_format)           (GstAmfEncoder * encoder,
                                       GstVideoCodecState * state,
                                       gpointer component);

  gboolean    (*set_output_state)     (GstAmfEncoder * encoder,
                                       GstVideoCodecState * state,
                                       gpointer component);

  gboolean    (*set_surface_prop)     (GstAmfEncoder * encoder,
                                       GstVideoCodecFrame * frame,
                                       gpointer surface);

  GstBuffer * (*create_output_buffer) (GstAmfEncoder * encoder,
                                       gpointer data,
                                       gboolean * sync_point);

  gboolean    (*check_reconfigure)    (GstAmfEncoder * encoder);
};

GType gst_amf_encoder_get_type (void);

void  gst_amf_encoder_set_subclass_data (GstAmfEncoder * encoder,
                                         gint64 adapter_luid,
                                         const wchar_t * codec_id);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstAmfEncoder, gst_object_unref)

G_END_DECLS
