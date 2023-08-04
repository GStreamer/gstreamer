/*
 * GStreamer
 * Copyright (C) 2023 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <vector>
#include <stdio.h>

#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#include "gstqsgmaterial.h"

#define GST_CAT_DEFAULT gst_qsg_texture_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define ATTRIBUTE_POSITION_NAME "a_position"
#define ATTRIBUTE_TEXCOORD_NAME "a_texcoord"
#define UNIFORM_POSITION_MATRIX_NAME "u_transformation"
#define UNIFORM_OPACITY_NAME "opacity"
#define UNIFORM_SWIZZLE_COMPONENTS_NAME "swizzle_components"
#define UNIFORM_TEXTURE0_NAME "tex"
#define UNIFORM_YUV_OFFSET_NAME "yuv_offset"
#define UNIFORM_YUV_YCOEFF_NAME "yuv_ycoeff"
#define UNIFORM_YUV_UCOEFF_NAME "yuv_ucoeff"
#define UNIFORM_YUV_VCOEFF_NAME "yuv_vcoeff"
#define UNIFORM_TRIPLANAR_PLANE0 "Ytex"
#define UNIFORM_TRIPLANAR_PLANE1 "Utex"
#define UNIFORM_TRIPLANAR_PLANE2 "Vtex"

/* matrices from glcolorconvert */
/* FIXME: use the colormatrix support from videoconvert */

/* BT. 601 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_yuv_bt601_offset[] = {-0.0625f, -0.5f, -0.5f};
static const gfloat from_yuv_bt601_rcoeff[] = {1.164f, 0.000f, 1.596f};
static const gfloat from_yuv_bt601_gcoeff[] = {1.164f,-0.391f,-0.813f};
static const gfloat from_yuv_bt601_bcoeff[] = {1.164f, 2.018f, 0.000f};

/* BT. 709 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat from_yuv_bt709_offset[] = {-0.0625f, -0.5f, -0.5f};
static const gfloat from_yuv_bt709_rcoeff[] = {1.164f, 0.000f, 1.787f};
static const gfloat from_yuv_bt709_gcoeff[] = {1.164f,-0.213f,-0.531f};
static const gfloat from_yuv_bt709_bcoeff[] = {1.164f,2.112f, 0.000f};

class GstQSGMaterialShader : public QSGMaterialShader {
public:
  GstQSGMaterialShader(GstVideoFormat v_format, char *vertex, char *fragment);
  ~GstQSGMaterialShader();

  void updateState(const RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override
  {
    Q_ASSERT(program()->isLinked());
    if (state.isMatrixDirty())
       program()->setUniformValue(m_id_matrix, state.combinedMatrix());
    if (state.isOpacityDirty())
      program()->setUniformValue(m_id_opacity, state.opacity());

    GstQSGMaterial *mat = static_cast<GstQSGMaterial *>(newMaterial);
    mat->bind(this, this->v_format);
  }

  char const *const *attributeNames() const override
  {
    static char const *const names[] = { ATTRIBUTE_POSITION_NAME, ATTRIBUTE_TEXCOORD_NAME, 0 };
    return names;
  }

  void initialize() override
  {
    const GstVideoFormatInfo *finfo = gst_video_format_get_info (v_format);
    QSGMaterialShader::initialize();
    m_id_matrix = program()->uniformLocation(UNIFORM_POSITION_MATRIX_NAME);
    m_id_opacity = program()->uniformLocation(UNIFORM_OPACITY_NAME);
    int swizzle_components = program()->uniformLocation(UNIFORM_SWIZZLE_COMPONENTS_NAME);
    int reorder[4];

    gst_gl_video_format_swizzle (v_format, reorder);
    program()->setUniformValueArray(swizzle_components, reorder, G_N_ELEMENTS (reorder));

    const char *tex_names[GST_VIDEO_MAX_PLANES];
    switch (v_format) {
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_BGRA:
        tex_names[0] = UNIFORM_TEXTURE0_NAME;
        break;
      case GST_VIDEO_FORMAT_YV12:
        tex_names[0] = UNIFORM_TRIPLANAR_PLANE0;
        tex_names[1] = UNIFORM_TRIPLANAR_PLANE1;
        tex_names[2] = UNIFORM_TRIPLANAR_PLANE2;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    for (guint i = 0; i < finfo->n_planes; i++) {
      this->tex_uniforms[i] = program()->uniformLocation(tex_names[i]);
      GST_TRACE ("%p tex uniform %i for tex %s", this, this->tex_uniforms[i], tex_names[i]);
    }

    this->cms_uniform_offset = program()->uniformLocation(UNIFORM_YUV_OFFSET_NAME);
    this->cms_uniform_ycoeff = program()->uniformLocation(UNIFORM_YUV_YCOEFF_NAME);
    this->cms_uniform_ucoeff = program()->uniformLocation(UNIFORM_YUV_UCOEFF_NAME);
    this->cms_uniform_vcoeff = program()->uniformLocation(UNIFORM_YUV_VCOEFF_NAME);
  }

  const char *vertexShader() const override;
  const char *fragmentShader() const override;

  int cms_uniform_offset;
  int cms_uniform_ycoeff;
  int cms_uniform_ucoeff;
  int cms_uniform_vcoeff;
  int tex_uniforms[GST_VIDEO_MAX_PLANES];

private:
  int m_id_matrix;
  int m_id_opacity;
  GstVideoFormat v_format;
  char *vertex;
  char *fragment;
};

GstQSGMaterialShader::GstQSGMaterialShader(GstVideoFormat v_format, char * vertex, char * fragment)
  : v_format(v_format),
  vertex(vertex),
  fragment(fragment)
{
}

GstQSGMaterialShader::~GstQSGMaterialShader()
{
  g_clear_pointer (&this->vertex, g_free);
  g_clear_pointer (&this->fragment, g_free);
}

const char *
GstQSGMaterialShader::vertexShader() const
{
  return vertex;
}

const char *
GstQSGMaterialShader::fragmentShader() const
{
  return fragment;
}

#define DEFINE_MATERIAL(format) \
class G_PASTE(GstQSGMaterial_,format) : public GstQSGMaterial { \
public: \
  G_PASTE(GstQSGMaterial_,format)(); \
  ~G_PASTE(GstQSGMaterial_,format)(); \
  QSGMaterialType *type() const override { static QSGMaterialType type; return &type; }; \
}; \
G_PASTE(GstQSGMaterial_,format)::G_PASTE(GstQSGMaterial_,format)() {} \
G_PASTE(GstQSGMaterial_,format)::~G_PASTE(GstQSGMaterial_,format)() {}

DEFINE_MATERIAL(RGBA);
DEFINE_MATERIAL(RGBA_SWIZZLE);
DEFINE_MATERIAL(YUV_TRIPLANAR);

GstQSGMaterial *
GstQSGMaterial::new_for_format(GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBA:
      return static_cast<GstQSGMaterial *>(new GstQSGMaterial_RGBA());
    case GST_VIDEO_FORMAT_BGRA:
      return static_cast<GstQSGMaterial *>(new GstQSGMaterial_RGBA_SWIZZLE());
    case GST_VIDEO_FORMAT_YV12:
      return static_cast<GstQSGMaterial *>(new GstQSGMaterial_YUV_TRIPLANAR());
    default:
      g_assert_not_reached ();
  }
}

GstQSGMaterial::GstQSGMaterial ()
{
  static gsize _debug;

  if (g_once_init_enter (&_debug)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "qtqsgmaterial", 0,
        "Qt Scenegraph Material");
    g_once_init_leave (&_debug, 1);
  }

  g_weak_ref_init (&this->qt_context_ref_, NULL);
  gst_video_info_init (&this->v_info);
  memset (&this->v_frame, 0, sizeof (this->v_frame));

  this->buffer_ = NULL;
  this->buffer_was_bound = FALSE;
  this->sync_buffer_ = gst_buffer_new ();
}

GstQSGMaterial::~GstQSGMaterial ()
{
  g_weak_ref_clear (&this->qt_context_ref_);
  gst_buffer_replace (&this->buffer_, NULL);
  gst_buffer_replace (&this->sync_buffer_, NULL);
  this->buffer_was_bound = FALSE;

  if (this->v_frame.buffer) {
    gst_video_frame_unmap (&this->v_frame);
    memset (&this->v_frame, 0, sizeof (this->v_frame));
  }
}

bool
GstQSGMaterial::compatibleWith(GstVideoInfo * v_info)
{
  if (GST_VIDEO_INFO_FORMAT (&this->v_info) != GST_VIDEO_INFO_FORMAT (v_info))
    return FALSE;

  return TRUE;
}

static char *
vertexShaderForFormat(GstVideoFormat v_format)
{
  return g_strdup (gst_gl_shader_string_vertex_mat4_vertex_transform);
}

#define qt_inputs \
  "attribute vec4 " ATTRIBUTE_POSITION ";\n" \
  "attribute vec2 " ATTRIBUTE_TEXCOORD ";\n" \

#define texcoord_input \
  "varying vec2 v_texcoord;\n"
#define single_texture_input \
  "uniform sampler2D " UNIFORM_TEXTURE0_NAME ";\n"
#define triplanar_texture_input \
  "uniform sampler2D " UNIFORM_TRIPLANAR_PLANE0 ";\n" \
  "uniform sampler2D " UNIFORM_TRIPLANAR_PLANE1 ";\n" \
  "uniform sampler2D " UNIFORM_TRIPLANAR_PLANE2 ";\n"

#define uniform_swizzle \
  "uniform int " UNIFORM_SWIZZLE_COMPONENTS_NAME "[4];\n"
#define uniform_opacity \
  "uniform float " UNIFORM_OPACITY_NAME ";\n"
#define uniform_yuv_to_rgb_color_matrix \
  "uniform vec3 " UNIFORM_YUV_OFFSET_NAME ";\n" \
  "uniform vec3 " UNIFORM_YUV_YCOEFF_NAME ";\n" \
  "uniform vec3 " UNIFORM_YUV_UCOEFF_NAME ";\n" \
  "uniform vec3 " UNIFORM_YUV_VCOEFF_NAME ";\n"

static char *
fragmentShaderForFormat(GstVideoFormat v_format, GstGLContext * context)
{
  switch (v_format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBA: {
      char *swizzle = gst_gl_color_convert_swizzle_shader_string (context);
      char *ret = g_strdup_printf (texcoord_input single_texture_input uniform_opacity
          "%s\n"
          "void main(void) {\n"
          "  gl_FragColor = texture2D(tex, v_texcoord) * " UNIFORM_OPACITY_NAME ";\n"
          "}\n", swizzle);
      g_clear_pointer (&swizzle, g_free);
      return ret;
    }
    case GST_VIDEO_FORMAT_BGRA: {
      char *swizzle = gst_gl_color_convert_swizzle_shader_string (context);
      char *ret = g_strdup_printf (texcoord_input single_texture_input uniform_swizzle uniform_opacity
          "%s\n"
          "void main(void) {\n"
          "  gl_FragColor = swizzle(texture2D(tex, v_texcoord), " UNIFORM_SWIZZLE_COMPONENTS_NAME ") * " UNIFORM_OPACITY_NAME ";\n"
          "}\n", swizzle);
      g_clear_pointer (&swizzle, g_free);
      return ret;
    }
    case GST_VIDEO_FORMAT_YV12: {
      char *yuv_to_rgb = gst_gl_color_convert_yuv_to_rgb_shader_string (context);
      char *swizzle = gst_gl_color_convert_swizzle_shader_string (context);
      char *ret = g_strdup_printf (texcoord_input triplanar_texture_input uniform_swizzle uniform_yuv_to_rgb_color_matrix uniform_opacity
        "%s\n"
        "%s\n"
        "void main(void) {\n"
        "  vec4 yuva, rgba;\n"
        "  yuva.x = texture2D(Ytex, v_texcoord).r;\n"
        "  yuva.y = texture2D(Utex, v_texcoord).r;\n"
        "  yuva.z = texture2D(Vtex, v_texcoord).r;\n"
        "  yuva.a = 1.0;\n"
        "  yuva = swizzle(yuva, " UNIFORM_SWIZZLE_COMPONENTS_NAME ");\n"
        "  rgba.rgb = yuv_to_rgb (yuva.xyz, " UNIFORM_YUV_OFFSET_NAME ", " UNIFORM_YUV_YCOEFF_NAME ", " UNIFORM_YUV_UCOEFF_NAME ", " UNIFORM_YUV_VCOEFF_NAME ");\n"
        "  rgba.a = yuva.a;\n"
        "  gl_FragColor = rgba * " UNIFORM_OPACITY_NAME ";\n"
        //"  gl_FragColor = vec4(yuva.x, 0.0, 0.0, 1.0);\n"
        "}\n", yuv_to_rgb, swizzle);
      g_clear_pointer (&yuv_to_rgb, g_free);
      g_clear_pointer (&swizzle, g_free);
      return ret;
    }
    default:
      return NULL;
  }
}

QSGMaterialShader *
GstQSGMaterial::createShader() const
{
  GstVideoFormat v_format = GST_VIDEO_INFO_FORMAT (&this->v_info);
  char *vertex = vertexShaderForFormat(v_format);
  char *fragment = fragmentShaderForFormat(v_format, NULL);

  if (!vertex || !fragment)
    return nullptr;

  return new GstQSGMaterialShader(v_format, vertex, fragment);
}

/* only called from the streaming thread with scene graph thread blocked */
void
GstQSGMaterial::setCaps (GstCaps * caps)
{
  GST_LOG ("%p setCaps %" GST_PTR_FORMAT, this, caps);

  gst_video_info_from_caps (&this->v_info, caps);
}

/* only called from the streaming thread with scene graph thread blocked */
gboolean
GstQSGMaterial::setBuffer (GstBuffer * buffer)
{
  GST_LOG ("%p setBuffer %" GST_PTR_FORMAT, this, buffer);
  /* FIXME: update more state here */
  if (!gst_buffer_replace (&this->buffer_, buffer))
    return FALSE;

  this->buffer_was_bound = FALSE;

  g_weak_ref_set (&this->qt_context_ref_, gst_gl_context_get_current ());

  return TRUE;
}

/* only called from the streaming thread with scene graph thread blocked */
GstBuffer *
GstQSGMaterial::getBuffer (gboolean * was_bound)
{
  GstBuffer *buffer = NULL;

  if (this->buffer_)
    buffer = gst_buffer_ref (this->buffer_);
  if (was_bound)
    *was_bound = this->buffer_was_bound;

  return buffer;
}

void
GstQSGMaterial::bind(GstQSGMaterialShader *shader, GstVideoFormat v_format)
{
  const GstGLFuncs *gl;
  GstGLContext *context, *qt_context;
  GstGLSyncMeta *sync_meta;
  GstMemory *mem;
  gboolean use_dummy_tex = TRUE;

  if (this->v_frame.buffer) {
    gst_video_frame_unmap (&this->v_frame);
    memset (&this->v_frame, 0, sizeof (this->v_frame));
  }

  qt_context = GST_GL_CONTEXT (g_weak_ref_get (&this->qt_context_ref_));
  if (!qt_context)
    goto out;

  if (!this->buffer_)
    goto out;
  if (GST_VIDEO_INFO_FORMAT (&this->v_info) == GST_VIDEO_FORMAT_UNKNOWN)
    goto out;

  this->mem_ = gst_buffer_peek_memory (this->buffer_, 0);
  if (!this->mem_)
    goto out;

  gl = qt_context->gl_vtable;

  /* FIXME: should really lock the memory to prevent write access */
  if (!gst_video_frame_map (&this->v_frame, &this->v_info, this->buffer_,
        (GstMapFlags) (GST_MAP_READ | GST_MAP_GL))) {
    g_assert_not_reached ();
    goto out;
  }

  mem = gst_buffer_peek_memory (this->buffer_, 0);
  g_assert (gst_is_gl_memory (mem));

  context = ((GstGLBaseMemory *)mem)->context;

  sync_meta = gst_buffer_get_gl_sync_meta (this->sync_buffer_);
  if (!sync_meta)
    sync_meta = gst_buffer_add_gl_sync_meta (context, this->sync_buffer_);

  gst_gl_sync_meta_set_sync_point (sync_meta, context);

  gst_gl_sync_meta_wait (sync_meta, qt_context);

  if (this->v_frame.info.finfo->flags & GST_VIDEO_FORMAT_FLAG_YUV) {
    if (gst_video_colorimetry_matches (&this->v_frame.info.colorimetry,
            GST_VIDEO_COLORIMETRY_BT709)) {
      this->cms_offset = (gfloat *) from_yuv_bt709_offset;
      this->cms_ycoeff = (gfloat *) from_yuv_bt709_rcoeff;
      this->cms_ucoeff = (gfloat *) from_yuv_bt709_gcoeff;
      this->cms_vcoeff = (gfloat *) from_yuv_bt709_bcoeff;
    } else {
      /* defaults/bt601 */
      this->cms_offset = (gfloat *) from_yuv_bt601_offset;
      this->cms_ycoeff = (gfloat *) from_yuv_bt601_rcoeff;
      this->cms_ucoeff = (gfloat *) from_yuv_bt601_gcoeff;
      this->cms_vcoeff = (gfloat *) from_yuv_bt601_bcoeff;
    }

    shader->program()->setUniformValue(shader->cms_uniform_offset, QVector3D(this->cms_offset[0], this->cms_offset[1], this->cms_offset[2]));
    shader->program()->setUniformValue(shader->cms_uniform_ycoeff, QVector3D(this->cms_ycoeff[0], this->cms_ycoeff[1], this->cms_ycoeff[2]));
    shader->program()->setUniformValue(shader->cms_uniform_ucoeff, QVector3D(this->cms_ucoeff[0], this->cms_ucoeff[1], this->cms_ucoeff[2]));
    shader->program()->setUniformValue(shader->cms_uniform_vcoeff, QVector3D(this->cms_vcoeff[0], this->cms_vcoeff[1], this->cms_vcoeff[2]));
  } else {
    this->cms_offset = this->cms_ycoeff = this->cms_ucoeff = this->cms_vcoeff = NULL;
  }

  /* reversed iteration order so that glActiveTexture(GL_TEXTURE0) is last which keeps
   * us in the default GL state expected by several other qml components
   */
  for (int i = GST_VIDEO_FRAME_N_PLANES (&this->v_frame) - 1; i >= 0; i--) {
    guint tex_id = *(guint *) this->v_frame.data[i];
    shader->program()->setUniformValue(shader->tex_uniforms[i], i);
    gl->ActiveTexture (GL_TEXTURE0 + i);
    GST_LOG ("%p binding for plane %d Qt texture %u", this, i, tex_id);

    gl->BindTexture (GL_TEXTURE_2D, tex_id);
  }

  /* Texture was successfully bound, so we do not need
   * to use the dummy texture */
  use_dummy_tex = FALSE;

  this->buffer_was_bound = TRUE;

out:
  gst_clear_object (&qt_context);

  if (G_UNLIKELY (use_dummy_tex)) {
    QOpenGLContext *qglcontext = QOpenGLContext::currentContext ();
    QOpenGLFunctions *funcs = qglcontext->functions ();
    const GstVideoFormatInfo *finfo = gst_video_format_get_info (v_format);

    /* Create dummy texture if not already present.
     * Use the Qt OpenGL functions instead of the GstGL ones,
     * since we are using the Qt OpenGL context here, and we must
     * be able to delete the texture in the destructor. */
    for (int i = finfo->n_planes - 1; i >= 0; i--) {
      shader->program()->setUniformValue(shader->tex_uniforms[i], i);
      funcs->glActiveTexture(GL_TEXTURE0 + i);

      if (this->dummy_textures[i] == 0) {
        /* Make this a black 64x64 pixel RGBA texture.
         * This size and format is supported pretty much everywhere, so these
         * are a safe pick. (64 pixel sidelength must be supported according
         * to the GLES2 spec, table 6.18.)
         * Set min/mag filters to GL_LINEAR to make sure no mipmapping is used. */
        const int tex_sidelength = 64;

        std::vector < guint8 > dummy_data (tex_sidelength * tex_sidelength * 4, 0);
        switch (v_format) {
          case GST_VIDEO_FORMAT_RGBA:
          case GST_VIDEO_FORMAT_BGRA:
          case GST_VIDEO_FORMAT_RGB:
            break;
          case GST_VIDEO_FORMAT_YV12:
            if (i == 1 || i == 2) {
              guint8 *data = dummy_data.data();
              for (gsize j = 0; j < tex_sidelength; j++) {
                for (gsize k = 0; k < tex_sidelength; k++) {
                  data[(j * tex_sidelength + k) * 4 + 0] = 0x7F;
                }
              }
            }
            break;
          default:
            g_assert_not_reached ();
            break;
        }

        funcs->glGenTextures (1, &this->dummy_textures[i]);
        funcs->glBindTexture (GL_TEXTURE_2D, this->dummy_textures[i]);
        funcs->glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        funcs->glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        funcs->glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, tex_sidelength,
            tex_sidelength, 0, GL_RGBA, GL_UNSIGNED_BYTE, &dummy_data[0]);
      }

      g_assert (this->dummy_textures[i] != 0);

      funcs->glBindTexture (GL_TEXTURE_2D, this->dummy_textures[i]);
      GST_LOG ("%p binding for plane %d fallback dummy Qt texture %u", this, i, this->dummy_textures[i]);
    }
  }
}
