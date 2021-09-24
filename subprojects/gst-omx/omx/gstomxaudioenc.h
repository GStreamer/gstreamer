/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_AUDIO_ENC_H__
#define __GST_OMX_AUDIO_ENC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudioencoder.h>

#include "gstomx.h"

G_BEGIN_DECLS

#define GST_TYPE_OMX_AUDIO_ENC \
  (gst_omx_audio_enc_get_type())
#define GST_OMX_AUDIO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_AUDIO_ENC,GstOMXAudioEnc))
#define GST_OMX_AUDIO_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_AUDIO_ENC,GstOMXAudioEncClass))
#define GST_OMX_AUDIO_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_AUDIO_ENC,GstOMXAudioEncClass))
#define GST_IS_OMX_AUDIO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_AUDIO_ENC))
#define GST_IS_OMX_AUDIO_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_AUDIO_ENC))

typedef struct _GstOMXAudioEnc GstOMXAudioEnc;
typedef struct _GstOMXAudioEncClass GstOMXAudioEncClass;

struct _GstOMXAudioEnc
{
  GstAudioEncoder parent;

  /* < protected > */
  GstOMXComponent *enc;
  GstOMXPort *enc_in_port, *enc_out_port;

  /* < private > */
  /* TRUE if the component is configured and saw
   * the first buffer */
  gboolean started;

  GstClockTime last_upstream_ts;

  /* Draining state */
  GMutex drain_lock;
  GCond drain_cond;
  /* TRUE if EOS buffers shouldn't be forwarded */
  gboolean draining;

  GstFlowReturn downstream_flow_ret;
};

struct _GstOMXAudioEncClass
{
  GstAudioEncoderClass parent_class;

  GstOMXClassData cdata;

  gboolean (*set_format)       (GstOMXAudioEnc * self, GstOMXPort * port, GstAudioInfo * info);
  GstCaps *(*get_caps)         (GstOMXAudioEnc * self, GstOMXPort * port, GstAudioInfo * info);
  guint    (*get_num_samples)  (GstOMXAudioEnc * self, GstOMXPort * port, GstAudioInfo * info, GstOMXBuffer * buffer);
};

GType gst_omx_audio_enc_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_AUDIO_ENC_H__ */
