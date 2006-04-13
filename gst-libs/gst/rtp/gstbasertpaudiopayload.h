/* GStreamer
 * Copyright (C) <2006> Philippe Khalaf <burger@speedy.org> 
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

#ifndef __GST_BASE_RTP_AUDIO_PAYLOAD_H__
#define __GST_BASE_RTP_AUDIO_PAYLOAD_H__

#include <gst/gst.h>
#include <gst/rtp/gstbasertppayload.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

typedef struct _GstBaseRTPAudioPayload GstBaseRTPAudioPayload;
typedef struct _GstBaseRTPAudioPayloadClass GstBaseRTPAudioPayloadClass;

#define GST_TYPE_BASE_RTP_AUDIO_PAYLOAD \
  (gst_basertpaudiopayload_get_type())
#define GST_BASE_RTP_AUDIO_PAYLOAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_RTP_AUDIO_PAYLOAD,GstBaseRTPAudioPayload))
#define GST_BASE_RTP_AUDIO_PAYLOAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_RTP_AUDIO_PAYLOAD,GstBaseRTPAudioPayload))
#define GST_IS_BASE_RTP_AUDIO_PAYLOAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_RTP_AUDIO_PAYLOAD))
#define GST_IS_BASE_RTP_AUDIO_PAYLOAD_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_RTP_AUDIO_PAYLOAD))

typedef enum {
  AUDIO_CODEC_TYPE_NONE,
  AUDIO_CODEC_TYPE_FRAME_BASED,
  AUDIO_CODEC_TYPE_SAMPLE_BASED
} AudioCodecType;

struct _GstBaseRTPAudioPayload
{
  GstBaseRTPPayload payload;

  GstClockTime adapter_base_ts;
  GstAdapter *adapter;
  gint frame_size;
  gint frame_duration;

  gint sample_size;

  AudioCodecType type;
};

struct _GstBaseRTPAudioPayloadClass
{
  GstBaseRTPPayloadClass parent_class;
};

gboolean gst_basertpaudiopayload_plugin_init (GstPlugin * plugin);

GType gst_basertpaudiopayload_get_type (void);

void
gst_basertpaudiopayload_set_frame_based (GstBaseRTPAudioPayload *basertpaudiopayload);

void
gst_basertpaudiopayload_set_sample_based (GstBaseRTPAudioPayload *basertpaudiopayload);

void
gst_basertpaudiopayload_set_frame_options (GstBaseRTPAudioPayload
    *basertpaudiopayload, gint frame_duration, gint frame_size);

void
gst_basertpaudiopayload_set_sample_options (GstBaseRTPAudioPayload
    *basertpaudiopayload, gint sample_size);

G_END_DECLS

#endif /* __GST_BASE_RTP_AUDIO_PAYLOAD_H__ */
