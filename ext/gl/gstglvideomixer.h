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

#ifndef _GST_GL_VIDEO_MIXER_H_
#define _GST_GL_VIDEO_MIXER_H_

#include "gstglmixer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_VIDEO_MIXER            (gst_gl_video_mixer_get_type())
#define GST_GL_VIDEO_MIXER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_VIDEO_MIXER,GstGLVideoMixer))
#define GST_IS_GL_VIDEO_MIXER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_VIDEO_MIXER))
#define GST_GL_VIDEO_MIXER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_VIDEO_MIXER,GstGLVideoMixerClass))
#define GST_IS_GL_VIDEO_MIXER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_VIDEO_MIXER))
#define GST_GL_VIDEO_MIXER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_VIDEO_MIXER,GstGLVideoMixerClass))

typedef struct _GstGLVideoMixer GstGLVideoMixer;
typedef struct _GstGLVideoMixerClass GstGLVideoMixerClass;

/**
 * GstGLVideoMixerBackground:
 * @GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER: checker pattern background
 * @GST_GL_VIDEO_MIXER_BACKGROUND_BLACK: solid color black background
 * @GST_GL_VIDEO_MIXER_BACKGROUND_WHITE: solid color white background
 * @GST_GL_VIDEO_MIXER_BACKGROUND_TRANSPARENT: background is left transparent and layers are composited using "A OVER B" composition rules. This is only applicable to AYUV and ARGB (and variants) as it preserves the alpha channel and allows for further mixing.
 *
 * The different backgrounds compositor can blend over.
 */
typedef enum
{
  GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER,
  GST_GL_VIDEO_MIXER_BACKGROUND_BLACK,
  GST_GL_VIDEO_MIXER_BACKGROUND_WHITE,
  GST_GL_VIDEO_MIXER_BACKGROUND_TRANSPARENT,
}
GstGLVideoMixerBackground;

struct _GstGLVideoMixer
{
    GstGLMixer mixer;

    GstGLVideoMixerBackground background;

    GstGLShader *shader;
    GstGLShader *checker;
    GPtrArray *input_frames;

    GLuint vao;
    GLuint checker_vbo;
};

struct _GstGLVideoMixerClass
{
    GstGLMixerClass mixer_class;
};

GType gst_gl_video_mixer_get_type (void);
GType gst_gl_video_mixer_bin_get_type (void);

G_END_DECLS

#endif /* _GST_GLFILTERCUBE_H_ */
