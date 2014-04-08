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

/**
 * SECTION:element-glvideomixer
 *
 * glmixer sub element. N gl sink pads to 1 source pad.
 * N + 1 OpenGL contexts shared together.
 * N <= 6 because the rendering is more a like a cube than a video_mixer
 * Each opengl input stream is rendered on a cube face
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-0.10 videotestsrc ! "video/x-raw-yuv, format=(fourcc)YUY2" ! glupload ! queue ! glvideomixer name=m ! glimagesink videotestsrc pattern=12 ! "video/x-raw-yuv, format=(fourcc)I420, framerate=(fraction)5/1, width=100, height=200" ! glupload ! queue ! m. videotestsrc ! "video/x-raw-rgb, framerate=(fraction)15/1, width=1500, height=1500" ! glupload ! gleffects effect=3 ! queue ! m. videotestsrc ! glupload ! gleffects effect=2 ! queue ! m.  videotestsrc ! glupload ! glfiltercube ! queue ! m. videotestsrc ! glupload ! gleffects effect=6 ! queue ! m.
 * ]|
 * FBO (Frame Buffer Object) is required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglvideomixer.h"

#define GST_CAT_DEFAULT gst_gl_video_mixer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
};

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_gl_video_mixer_debug, "glvideomixer", 0, "glvideomixer element");

G_DEFINE_TYPE_WITH_CODE (GstGLVideoMixer, gst_gl_video_mixer, GST_TYPE_GL_MIXER,
    DEBUG_INIT);

static void gst_gl_video_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_video_mixer_reset (GstGLMixer * mixer);
static gboolean gst_gl_video_mixer_init_shader (GstGLMixer * mixer,
    GstCaps * outcaps);

static gboolean gst_gl_video_mixer_process_textures (GstGLMixer * mixer,
    GPtrArray * in_frames, guint out_tex);
static void gst_gl_video_mixer_callback (gpointer stuff);

/* vertex source */
static const gchar *video_mixer_v_src =
    "attribute vec4 a_position;                                   \n"
    "attribute vec2 a_texCoord;                                   \n"
    "uniform float x_scale;                                       \n"
    "uniform float y_scale;                                       \n"
    "varying vec2 v_texCoord;                                     \n"
    "void main()                                                  \n"
    "{                                                            \n"
    "   gl_Position = a_position * vec4(x_scale, y_scale, 1.0, 1.0);\n"
    "   v_texCoord = a_texCoord;                                  \n" "}";

/* fragment source */
static const gchar *video_mixer_f_src =
    "uniform sampler2D texture;                     \n"
    "varying vec2 v_texCoord;                            \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  vec4 rgba = texture2D( texture, v_texCoord );\n"
    "  gl_FragColor = vec4(rgba.rgb, 1.0);\n"
    "}                                                   \n";

static void
gst_gl_video_mixer_class_init (GstGLVideoMixerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_video_mixer_set_property;
  gobject_class->get_property = gst_gl_video_mixer_get_property;

  gst_element_class_set_metadata (element_class, "OpenGL video_mixer",
      "Filter/Effect/Video", "OpenGL video_mixer",
      "Julien Isorce <julien.isorce@gmail.com>");

  GST_GL_MIXER_CLASS (klass)->set_caps = gst_gl_video_mixer_init_shader;
  GST_GL_MIXER_CLASS (klass)->reset = gst_gl_video_mixer_reset;
  GST_GL_MIXER_CLASS (klass)->process_textures =
      gst_gl_video_mixer_process_textures;
}

static void
gst_gl_video_mixer_init (GstGLVideoMixer * video_mixer)
{
  video_mixer->shader = NULL;
  video_mixer->input_frames = NULL;
}

static void
gst_gl_video_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_video_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_video_mixer_reset (GstGLMixer * mixer)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mixer);

  video_mixer->input_frames = NULL;

  if (video_mixer->shader)
    gst_gl_context_del_shader (mixer->context, video_mixer->shader);
  video_mixer->shader = NULL;
}

static gboolean
gst_gl_video_mixer_init_shader (GstGLMixer * mixer, GstCaps * outcaps)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mixer);

  return gst_gl_context_gen_shader (mixer->context, video_mixer_v_src,
      video_mixer_f_src, &video_mixer->shader);
}

static gboolean
gst_gl_video_mixer_process_textures (GstGLMixer * mix, GPtrArray * frames,
    guint out_tex)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mix);

  video_mixer->input_frames = frames;

  gst_gl_context_use_fbo_v2 (mix->context,
      GST_VIDEO_INFO_WIDTH (&mix->out_info),
      GST_VIDEO_INFO_HEIGHT (&mix->out_info), mix->fbo, mix->depthbuffer,
      out_tex, gst_gl_video_mixer_callback, (gpointer) video_mixer);

  return TRUE;
}

/* opengl scene, params: input texture (not the output mixer->texture) */
static void
gst_gl_video_mixer_callback (gpointer stuff)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (stuff);
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  GstGLFuncs *gl = mixer->context->gl_vtable;

  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;
  guint out_width, out_height;

  const GLushort indices[] = {
    0, 1, 2,
    0, 2, 3
  };

  guint count = 0;

  out_width = GST_VIDEO_INFO_WIDTH (&mixer->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&mixer->out_info);

  gst_gl_context_clear_shader (mixer->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);
  gl->Disable (GL_TEXTURE_2D);

  gl->Disable (GL_DEPTH_TEST);
  gl->Disable (GL_CULL_FACE);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (video_mixer->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (video_mixer->shader, "a_position");
  attr_texture_loc =
      gst_gl_shader_get_attribute_location (video_mixer->shader, "a_texCoord");

  gl->Enable (GL_BLEND);

  while (count < video_mixer->input_frames->len) {
    GstGLMixerFrameData *frame;
    /* *INDENT-OFF* */
    gfloat v_vertices[] = {
      /* front face */
      -1.0,-1.0,-1.0f, 0.0f, 0.0f,
       1.0,-1.0,-1.0f, 1.0f, 0.0f,
       1.0, 1.0,-1.0f, 1.0f, 1.0f,
      -1.0, 1.0,-1.0f, 0.0f, 1.0f,
    };
    /* *INDENT-ON* */
    guint in_tex;
    guint in_width, in_height;
    gfloat w, h;

    frame = g_ptr_array_index (video_mixer->input_frames, count);
    if (!frame) {
      GST_DEBUG ("skipping texture, null frame");
      count++;
      continue;
    }
    in_width = GST_VIDEO_INFO_WIDTH (&frame->pad->in_info);
    in_height = GST_VIDEO_INFO_HEIGHT (&frame->pad->in_info);

    if (!frame->texture || in_width <= 0 || in_height <= 0) {
      GST_DEBUG ("skipping texture:%u frame:%p width:%u height %u",
          frame->texture, frame, in_width, in_height);
      count++;
      continue;
    }
    in_tex = frame->texture;

    GST_TRACE ("processing texture:%u dimensions:%ux%u", in_tex, in_width,
        in_height);

    w = ((gfloat) in_width / (gfloat) out_width);
    h = ((gfloat) in_height / (gfloat) out_height);
    GST_TRACE ("processing texture:%u dimensions:%ux%u, %fx%f", in_tex,
        in_width, in_height, w, h);

    gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), &v_vertices[0]);

    gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), &v_vertices[3]);

    gl->EnableVertexAttribArray (attr_position_loc);
    gl->EnableVertexAttribArray (attr_texture_loc);

    gl->BlendFunc (GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR);
    gl->BlendEquation (GL_FUNC_ADD);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->BindTexture (GL_TEXTURE_2D, in_tex);
    gst_gl_shader_set_uniform_1i (video_mixer->shader, "texture", 0);
    gst_gl_shader_set_uniform_1f (video_mixer->shader, "x_scale", w);
    gst_gl_shader_set_uniform_1f (video_mixer->shader, "y_scale", h);

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    ++count;
  }

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->Disable (GL_BLEND);

  gst_gl_context_clear_shader (mixer->context);
}
