/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstaudiobasevisualizer.c: base class for audio visualisation elements
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GST_AUDIO_BASE_VISUALIZER_H__
#define __GST_AUDIO_BASE_VISUALIZER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS
#define GST_TYPE_AUDIO_BASE_VISUALIZER            (gst_audio_base_visualizer_get_type())
#define GST_AUDIO_BASE_VISUALIZER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_BASE_VISUALIZER,GstAudioBaseVisualizer))
#define GST_AUDIO_BASE_VISUALIZER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_BASE_VISUALIZER,GstAudioBaseVisualizerClass))
#define GST_IS_SYNAESTHESIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_BASE_VISUALIZER))
#define GST_IS_SYNAESTHESIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_BASE_VISUALIZER))
typedef struct _GstAudioBaseVisualizer GstAudioBaseVisualizer;
typedef struct _GstAudioBaseVisualizerClass GstAudioBaseVisualizerClass;

typedef void (*GstAudioBaseVisualizerShaderFunc)(GstAudioBaseVisualizer *scope, const guint8 *s, guint8 *d);

/**
 * GstAudioBaseVisualizerShader:
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_NONE: no shading
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE: plain fading
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_UP: fade and move up
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_DOWN: fade and move down
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_LEFT: fade and move left
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_RIGHT: fade and move right
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_OUT: fade and move horizontally out
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_IN: fade and move horizontally in
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_OUT: fade and move vertically out
 * @GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_IN: fade and move vertically in
 *
 * Different types of supported background shading functions.
 */
typedef enum {
  GST_AUDIO_BASE_VISUALIZER_SHADER_NONE,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_UP,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_DOWN,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_LEFT,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_RIGHT,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_OUT,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_IN,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_OUT,
  GST_AUDIO_BASE_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_IN
} GstAudioBaseVisualizerShader;

struct _GstAudioBaseVisualizer
{
  GstElement parent;

  /* pads */
  GstPad *srcpad, *sinkpad;

  GstBufferPool *pool;
  GstAdapter *adapter;
  GstBuffer *inbuf;
  guint8 *pixelbuf;

  GstAudioBaseVisualizerShader shader_type;
  GstAudioBaseVisualizerShaderFunc shader;
  guint32 shade_amount;

  guint spf;                    /* samples per video frame */
  guint req_spf;                /* min samples per frame wanted by the subclass */

  /* video state */
  GstVideoInfo vinfo;
  GstVideoFormat video_format;
  gint fps_n, fps_d;
  gint width;
  gint height;
  guint64 frame_duration;
  guint bpf;                    /* bytes per frame */

  /* audio state */
  GstAudioInfo ainfo;

  /* configuration mutex */
  GMutex config_lock;
  
  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;
  
  GstSegment segment;
};

struct _GstAudioBaseVisualizerClass
{
  GstElementClass parent_class;

  /* virtual function, called whenever the format changes */
  gboolean (*setup) (GstAudioBaseVisualizer * scope);

  /* virtual function for rendering a frame */
  gboolean (*render) (GstAudioBaseVisualizer * scope, GstBuffer * audio, GstBuffer * video);
};

GType gst_audio_base_visualizer_get_type (void);

G_END_DECLS
#endif /* __GST_AUDIO_BASE_VISUALIZER_H__ */
