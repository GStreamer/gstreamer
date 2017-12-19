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

#ifndef __GST_GL_BASE_MIXER_H__
#define __GST_GL_BASE_MIXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include <gst/video/gstvideoaggregator.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_BASE_MIXER_PAD (gst_gl_base_mixer_pad_get_type())
#define GST_GL_BASE_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_BASE_MIXER_PAD, GstGLBaseMixerPad))
#define GST_GL_BASE_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_BASE_MIXER_PAD, GstGLBaseMixerPadClass))
#define GST_IS_GL_BASE_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_BASE_MIXER_PAD))
#define GST_IS_GL_BASE_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_BASE_MIXER_PAD))
#define GST_GL_BASE_MIXER_PAD_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_GL_BASE_MIXER_PAD,GstGLBaseMixerPadClass))

typedef struct _GstGLBaseMixerPad GstGLBaseMixerPad;
typedef struct _GstGLBaseMixerPadClass GstGLBaseMixerPadClass;

/* all information needed for one video stream */
struct _GstGLBaseMixerPad
{
  GstVideoAggregatorPad parent;                /* subclass the pad */
};

struct _GstGLBaseMixerPadClass
{
  GstVideoAggregatorPadClass parent_class;
};

GType gst_gl_base_mixer_pad_get_type (void);

#define GST_TYPE_GL_BASE_MIXER (gst_gl_base_mixer_get_type())
#define GST_GL_BASE_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_BASE_MIXER, GstGLBaseMixer))
#define GST_GL_BASE_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_BASE_MIXER, GstGLBaseMixerClass))
#define GST_IS_GL_BASE_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_BASE_MIXER))
#define GST_IS_GL_BASE_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_BASE_MIXER))
#define GST_GL_BASE_MIXER_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_GL_BASE_MIXER,GstGLBaseMixerClass))

typedef struct _GstGLBaseMixer GstGLBaseMixer;
typedef struct _GstGLBaseMixerClass GstGLBaseMixerClass;
typedef struct _GstGLBaseMixerPrivate GstGLBaseMixerPrivate;

struct _GstGLBaseMixer
{
  GstVideoAggregator     vaggregator;

  GstGLDisplay          *display;
  GstGLContext          *context;

  gpointer _padding[GST_PADDING];

  GstGLBaseMixerPrivate *priv;
};

struct _GstGLBaseMixerClass
{
  GstVideoAggregatorClass parent_class;
  GstGLAPI supported_gl_api;

  gpointer _padding[GST_PADDING];
};

GType gst_gl_base_mixer_get_type(void);

G_END_DECLS
#endif /* __GST_GL_BASE_MIXER_H__ */
