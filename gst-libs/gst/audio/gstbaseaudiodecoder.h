/* GStreamer
 * Copyright (C) 2009 Igalia S.L.
 * Author: Iago Toral Quiroga <itoral@igalia.com>
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

#ifndef _GST_BASE_AUDIO_DECODER_H_
#define _GST_BASE_AUDIO_DECODER_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstBaseAudioDecoder is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/audio/gstbaseaudiocodec.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_AUDIO_DECODER \
  (gst_base_audio_decoder_get_type())
#define GST_BASE_AUDIO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_AUDIO_DECODER,GstBaseAudioDecoder))
#define GST_BASE_AUDIO_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_AUDIO_DECODER,GstBaseAudioDecoderClass))
#define GST_BASE_AUDIO_DECODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_AUDIO_DECODER,GstBaseAudioDecoderClass))
#define GST_IS_BASE_AUDIO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_AUDIO_DECODER))
#define GST_IS_BASE_AUDIO_DECODER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_AUDIO_DECODER))

/**
 * GST_BASE_AUDIO_DECODER_FLOW_NEED_DATA:
 *
 * Custom GstFlowReturn value indicating that more data is needed.
 */
#define GST_BASE_AUDIO_DECODER_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

typedef struct _GstBaseAudioDecoder GstBaseAudioDecoder;
typedef struct _GstBaseAudioDecoderClass GstBaseAudioDecoderClass;

struct _GstBaseAudioDecoder
{
  GstBaseAudioCodec base_audio_codec;

  /*< private >*/
  guint64 offset;
};

struct _GstBaseAudioDecoderClass
{
  GstBaseAudioCodecClass base_audio_codec_class;

  GstFlowReturn (*parse_data) (GstBaseAudioDecoder *decoder);
};

GType gst_base_audio_decoder_get_type (void);

G_END_DECLS

#endif

