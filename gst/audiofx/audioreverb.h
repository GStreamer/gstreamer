/* 
 * GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_AUDIO_REVERB_H__
#define __GST_AUDIO_REVERB_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_REVERB            (gst_audio_reverb_get_type())
#define GST_AUDIO_REVERB(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_REVERB,GstAudioReverb))
#define GST_IS_AUDIO_REVERB(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_REVERB))
#define GST_AUDIO_REVERB_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_AUDIO_REVERB,GstAudioReverbClass))
#define GST_IS_AUDIO_REVERB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_AUDIO_REVERB))
#define GST_AUDIO_REVERB_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_AUDIO_REVERB,GstAudioReverbClass))
typedef struct _GstAudioReverb GstAudioReverb;
typedef struct _GstAudioReverbClass GstAudioReverbClass;

typedef void (*GstAudioReverbProcessFunc) (GstAudioReverb *, guint8 *, guint);

struct _GstAudioReverb
{
  GstAudioFilter audiofilter;

  guint64 delay;
  gfloat intensity;
  gfloat feedback;

  /* < private > */
  GstAudioReverbProcessFunc process;
  guint delay_frames;
  guint8 *buffer;
  guint buffer_pos;
  guint buffer_size;
  guint buffer_size_frames;
};

struct _GstAudioReverbClass
{
  GstAudioFilterClass parent;
};

GType gst_audio_reverb_get_type (void);

G_END_DECLS

#endif /* __GST_AUDIO_REVERB_H__ */
