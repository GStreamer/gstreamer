/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_MF_AUDIO_ENC_H__
#define __GST_MF_AUDIO_ENC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstmfutils.h"
#include "gstmftransform.h"

G_BEGIN_DECLS

#define GST_TYPE_MF_AUDIO_ENC           (gst_mf_audio_enc_get_type())
#define GST_MF_AUDIO_ENC(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MF_AUDIO_ENC,GstMFAudioEnc))
#define GST_MF_AUDIO_ENC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_MF_AUDIO_ENC,GstMFAudioEncClass))
#define GST_MF_AUDIO_ENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_MF_AUDIO_ENC,GstMFAudioEncClass))
#define GST_IS_MF_AUDIO_ENC(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MF_AUDIO_ENC))
#define GST_IS_MF_AUDIO_ENC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_MF_AUDIO_ENC))

typedef struct _GstMFAudioEnc GstMFAudioEnc;
typedef struct _GstMFAudioEncClass GstMFAudioEncClass;

struct _GstMFAudioEnc
{
  GstAudioEncoder parent;

  GstMFTransform *transform;
  guint64 sample_duration_in_mf;
  guint64 sample_count;
};

struct _GstMFAudioEncClass
{
  GstAudioEncoderClass parent_class;

  GUID codec_id;
  guint32 enum_flags;
  guint device_index;
  gint frame_samples;

  gboolean (*get_output_type) (GstMFAudioEnc * mfenc,
                               GstAudioInfo * info,
                               IMFMediaType ** output_type);

  gboolean (*get_input_type)  (GstMFAudioEnc * mfenc,
                               GstAudioInfo * info,
                               IMFMediaType ** input_type);

  gboolean (*set_src_caps)    (GstMFAudioEnc * mfenc,
                               GstAudioInfo * info);
};

GType gst_mf_audio_enc_get_type (void);

G_END_DECLS

#endif /* __GST_MF_AUDIO_ENC_H__ */