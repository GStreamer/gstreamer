/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef __GST_GL_MIXER_H__
#define __GST_GL_MIXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include <gst/gl/gstglmixerpad.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_MIXER (gst_gl_mixer_get_type())
#define GST_GL_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_MIXER, GstGLMixer))
#define GST_GL_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_MIXER, GstGLMixerClass))
#define GST_IS_GL_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_MIXER))
#define GST_IS_GL_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_MIXER))
#define GST_GL_MIXER_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_GL_MIXER,GstGLMixerClass))

#define GST_GL_MIXER_GET_LOCK(mix) (GST_GL_MIXER(mix)->lock)
#define GST_GL_MIXER_LOCK(mix)     (g_mutex_lock(&GST_GL_MIXER_GET_LOCK (mix)))
#define GST_GL_MIXER_UNLOCK(mix)   (g_mutex_unlock(&GST_GL_MIXER_GET_LOCK (mix)))

typedef struct _GstGLMixer GstGLMixer;
typedef struct _GstGLMixerClass GstGLMixerClass;
typedef struct _GstGLMixerPrivate GstGLMixerPrivate;
typedef struct _GstGLMixerFrameData GstGLMixerFrameData;

typedef gboolean (*GstGLMixerSetCaps) (GstGLMixer* mixer,
  GstCaps* outcaps);
typedef void (*GstGLMixerReset) (GstGLMixer *mixer);
typedef gboolean (*GstGLMixerProcessFunc) (GstGLMixer *mix,
  GPtrArray *buffers, GstBuffer *outbuf);
typedef gboolean (*GstGLMixerProcessTextures) (GstGLMixer *mix,
  GPtrArray *frames, guint out_tex);

struct _GstGLMixer
{
  GstElement element;

  GstGLMixerPrivate *priv;

  /* pad */
  GstPad *srcpad;

  /* Lock to prevent the state to change while blending */
  GMutex lock;
  /* Sink pads using Collect Pads from core's base library */
  GstCollectPads *collect;

  /* sinkpads, a GSList of GstGLMixerPads */
  GSList *sinkpads;
  gint numpads;
  /* Next available sinkpad index */
  gint next_sinkpad;

  GPtrArray *array_buffers;
  GPtrArray *frames;

  GstCaps *current_caps;
  GstVideoInfo out_info;
  GLuint out_tex_id;
  GstGLDownload *download;

  gboolean newseg_pending;
  gboolean flush_stop_pending;
  gboolean send_stream_start;
  gboolean send_caps;

  GstSegment segment;
  GstClockTime ts_offset;
  guint64 nframes;

  /* sink event handling */
  gdouble proportion;
  GstClockTime earliest_time;
  guint64 qos_processed, qos_dropped;

  GstGLDisplay *display;
  GstGLContext *context;
  GLuint fbo;
  GLuint depthbuffer;

  GType pad_type;
};

struct _GstGLMixerClass
{
  GstElementClass parent_class;

  GstGLMixerSetCaps set_caps;
  GstGLMixerReset reset;
  GstGLMixerProcessFunc process_buffers;
  GstGLMixerProcessTextures process_textures;
};

struct _GstGLMixerFrameData
{
  GstGLMixerPad *pad;
  guint texture;
};

GType gst_gl_mixer_get_type(void);

gboolean gst_gl_mixer_process_textures (GstGLMixer * mix, GstBuffer * outbuf);

void gst_gl_mixer_set_pad_type (GstGLMixer * mix, GType pad_type);

G_END_DECLS
#endif /* __GST_GL_MIXER_H__ */
