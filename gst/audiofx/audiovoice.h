/* 
 * GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_AUDIO_VOICE_H__
#define __GST_AUDIO_VOICE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS
#define GST_TYPE_AUDIO_VOICE            (gst_audio_voice_get_type())
#define GST_AUDIO_VOICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_VOICE,GstAudioVoice))
#define GST_IS_AUDIO_VOICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_VOICE))
#define GST_AUDIO_VOICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_AUDIO_VOICE,GstAudioVoiceClass))
#define GST_IS_AUDIO_VOICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_AUDIO_VOICE))
#define GST_AUDIO_VOICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_AUDIO_VOICE,GstAudioVoiceClass))
typedef struct _GstAudioVoice GstAudioVoice;
typedef struct _GstAudioVoiceClass GstAudioVoiceClass;

typedef void (*GstAudioVoiceProcessFunc) (GstAudioVoice *, guint8 *, guint);

struct _GstAudioVoice
{
  GstAudioFilter audiofilter;

  gint channels;
  gint rate;

  /* properties */
  gfloat level;
  gfloat mono_level;
  gfloat filter_band;
  gfloat filter_width;

  /* filter coef */
  gfloat A, B, C;
  gfloat y1, y2;

  /* < private > */
  GstAudioVoiceProcessFunc process;
};

struct _GstAudioVoiceClass
{
  GstAudioFilterClass parent;
};

GType gst_audio_voice_get_type (void);

G_END_DECLS
#endif /* __GST_AUDIO_VOICE_H__ */
