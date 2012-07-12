/*
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfilter.h"

#define GST_CAT_DEFAULT gst_gl_filter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);


static GstStaticPadTemplate gst_gl_filter_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_filter_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Properties */
enum
{
  PROP_0,
  PROP_EXTERNAL_OPENGL_CONTEXT
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_debug, "glfilter", 0, "glfilter element");

G_DEFINE_TYPE_WITH_CODE (GstGLFilter, gst_gl_filter, GST_TYPE_BASE_TRANSFORM,
    DEBUG_INIT);

static void gst_gl_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstCaps *gst_gl_filter_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static void gst_gl_filter_reset (GstGLFilter * filter);
static gboolean gst_gl_filter_start (GstBaseTransform * bt);
static gboolean gst_gl_filter_stop (GstBaseTransform * bt);
static gboolean gst_gl_filter_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static GstFlowReturn gst_gl_filter_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_gl_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_gl_filter_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps);

/* GstGLDisplayThreadFunc */
static void gst_gl_filter_start_gl (GstGLDisplay * display, gpointer data);
static void gst_gl_filter_stop_gl (GstGLDisplay * display, gpointer data);

static void
gst_gl_filter_class_init (GstGLFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_filter_set_property;
  gobject_class->get_property = gst_gl_filter_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_filter_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_filter_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_filter_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_filter_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_filter_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->propose_allocation =
      gst_gl_filter_propose_allocation;
  GST_BASE_TRANSFORM_CLASS (klass)->decide_allocation =
      gst_gl_filter_decide_allocation;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_filter_get_unit_size;

  g_object_class_install_property (gobject_class, PROP_EXTERNAL_OPENGL_CONTEXT,
      g_param_spec_ulong ("external-opengl-context",
          "External OpenGL context",
          "Give an external OpenGL context with which to share textures",
          0, G_MAXULONG, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_sink_pad_template));

  klass->set_caps = NULL;
  klass->filter = NULL;
  klass->display_init_cb = NULL;
  klass->display_reset_cb = NULL;
  klass->onInitFBO = NULL;
  klass->onStart = NULL;
  klass->onStop = NULL;
  klass->onReset = NULL;
}

static void
gst_gl_filter_init (GstGLFilter * filter)
{
  GstBaseTransform *base_trans = GST_BASE_TRANSFORM (filter);

  gst_pad_set_query_function (base_trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_gl_filter_src_query));

  gst_gl_filter_reset (filter);
}

static void
gst_gl_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id) {
    case PROP_EXTERNAL_OPENGL_CONTEXT:
    {
      filter->external_gl_context = g_value_get_ulong (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;
  GstGLFilter *filter = GST_GL_FILTER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_writable_structure (query);
      if (filter->display) {
        /* this gl filter is a sink in terms of the gl chain */
        gst_structure_set (structure, "gstgldisplay", G_TYPE_POINTER,
            filter->display, NULL);
      } else {
        gchar *name;
        /* at least one gl element is after in our gl chain */

        name = gst_element_get_name (parent);
        res = g_strcmp0 (name, gst_structure_get_name (structure)) == 0;
        g_free (name);
      }
      if (!res)
        res = gst_pad_query_default (pad, parent, query);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static void
gst_gl_filter_reset (GstGLFilter * filter)
{
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (filter->display) {
    if (filter_class->onReset)
      filter_class->onReset (filter);

    if (filter_class->display_reset_cb != NULL) {
      gst_gl_display_thread_add (filter->display, gst_gl_filter_stop_gl,
          filter);
    }
    //blocking call, delete the FBO
    gst_gl_display_del_fbo (filter->display, filter->fbo, filter->depthbuffer);
    g_object_unref (filter->display);
    filter->display = NULL;
  }

  filter->width = 0;
  filter->height = 0;
  filter->fbo = 0;
  filter->depthbuffer = 0;
  filter->default_shader = NULL;
  filter->external_gl_context = 0;
}

static gboolean
gst_gl_filter_start (GstBaseTransform * bt)
{
  GstGLFilter *filter = GST_GL_FILTER (bt);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);
  GstElement *parent = GST_ELEMENT (gst_element_get_parent (filter));
  GstStructure *structure = NULL;
  GstQuery *query = NULL;
  gboolean isPerformed = FALSE;
  gchar *name;

  if (!parent) {
    GST_ELEMENT_ERROR (filter, CORE, STATE_CHANGE, (NULL),
        ("A parent bin is required"));
    return FALSE;
  }

  name = gst_element_get_name (filter);
  structure = gst_structure_new_empty (name);
  query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);
  g_free (name);

  isPerformed = gst_element_query (parent, query);

  if (isPerformed) {
    const GValue *id_value =
        gst_structure_get_value (structure, "gstgldisplay");
    if (G_VALUE_HOLDS_POINTER (id_value))
      /* at least one gl element is after in our gl chain */
      filter->display =
          g_object_ref (GST_GL_DISPLAY (g_value_get_pointer (id_value)));
    else {
      /* this gl filter is a sink in terms of the gl chain */
      filter->display = gst_gl_display_new ();
      isPerformed = gst_gl_display_create_context (filter->display,
          filter->external_gl_context);

      if (!isPerformed)
        GST_ELEMENT_ERROR (filter, RESOURCE, NOT_FOUND,
            GST_GL_DISPLAY_ERR_MSG (filter->display), (NULL));
    }
  }

  gst_query_unref (query);
  gst_object_unref (GST_OBJECT (parent));

  if (!isPerformed)
    return FALSE;

  if (filter_class->onStart)
    filter_class->onStart (filter);

  return TRUE;
}

static gboolean
gst_gl_filter_stop (GstBaseTransform * bt)
{
  GstGLFilter *filter = GST_GL_FILTER (bt);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (filter_class->onStop)
    filter_class->onStop (filter);

  gst_gl_filter_reset (filter);

  return TRUE;
}

static void
gst_gl_filter_start_gl (GstGLDisplay * display, gpointer data)
{
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->display_init_cb (filter);
}

static void
gst_gl_filter_stop_gl (GstGLDisplay * display, gpointer data)
{
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->display_reset_cb (filter);
}

static GstCaps *
gst_gl_filter_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  //GstGLFilter* filter = GST_GL_FILTER (bt);
  GstStructure *structure;
  GstCaps *newcaps, *result;
  const GValue *par;
  guint i, n;

  par = NULL;
  newcaps = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);
//  structure = gst_caps_get_structure (newcaps, 0);

  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);

    if (i > 0 && gst_caps_is_subset_structure (newcaps, structure))
      continue;

    structure = gst_structure_copy (structure);

//    gst_structure_set (structure,
//        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
//        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    if ((par = gst_structure_get_value (structure, "pixel-aspect-ratio"))) {
      gst_structure_set (structure,
          "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
          NULL);
    }

    gst_caps_append_structure (newcaps, structure);
  }

  if (filter) {
    result =
        gst_caps_intersect_full (filter, newcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (newcaps);
    newcaps = result;
  }

  GST_DEBUG_OBJECT (bt, "returning caps: %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}


static gboolean
gst_gl_filter_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static gboolean
gst_gl_filter_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoInfo info;
  GstGLFilter *filter;
  gboolean ret;
  GstGLFilterClass *filter_class;

  ret = FALSE;
  filter = GST_GL_FILTER (bt);
  filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (!filter->display)
    return FALSE;

  ret = gst_video_info_from_caps (&info, outcaps);
  filter->width = GST_VIDEO_INFO_WIDTH (&info);
  filter->height = GST_VIDEO_INFO_HEIGHT (&info);

  if (!ret) {
    GST_DEBUG ("bad caps");
    return FALSE;
  }
  //blocking call, generate a FBO
  ret = gst_gl_display_gen_fbo (filter->display, filter->width, filter->height,
      &filter->fbo, &filter->depthbuffer);

  if (!ret) {
    GST_ELEMENT_ERROR (filter, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (filter->display), (NULL));
    return FALSE;
  }

  if (filter_class->display_init_cb != NULL) {
    gst_gl_display_thread_add (filter->display, gst_gl_filter_start_gl, filter);
  }

  if (filter_class->onInitFBO)
    ret = filter_class->onInitFBO (filter);

  if (!ret) {
    GST_ELEMENT_ERROR (filter, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (filter->display), (NULL));
    return FALSE;
  }

  if (filter_class->set_caps)
    ret = filter_class->set_caps (filter, incaps, outcaps);

  if (!ret) {
    GST_ELEMENT_ERROR (filter, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (filter->display), (NULL));
    return FALSE;
  }

  GST_DEBUG ("set_caps %d %d", filter->width, filter->height);

  return ret;
}

static gboolean
gst_gl_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstGLFilter *filter = GST_GL_FILTER (trans);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = filter->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (filter, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (filter, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }
  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (filter, "create new pool");
    pool = gst_gl_buffer_pool_new (filter->display);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 1, 0);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);
  gst_query_add_allocation_meta (query, GST_GL_META_API_TYPE, 0);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (trans, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (trans, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (trans, "failed setting config");
    return FALSE;
  }
}

static gboolean
gst_gl_filter_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstGLFilter *filter = GST_GL_FILTER (trans);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  gst_query_parse_allocation (query, &caps, NULL);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool)
    pool = gst_gl_buffer_pool_new (filter->display);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_GL_META);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static GstFlowReturn
gst_gl_filter_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLFilter *filter;
  GstGLFilterClass *filter_class;

  filter = GST_GL_FILTER (bt);
  filter_class = GST_GL_FILTER_GET_CLASS (bt);

  if (filter_class->filter)
    filter_class->filter (filter, inbuf, outbuf);

  return GST_FLOW_OK;
}

/* convenience functions to simplify filter development */
void
gst_gl_filter_render_to_target (GstGLFilter * filter,
    GLuint input, GLuint target, GLCB func, gpointer data)
{
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
      filter->fbo, filter->depthbuffer, target,
      func,
      filter->width, filter->height, input,
      0, filter->width, 0, filter->height,
      GST_GL_DISPLAY_PROJECTION_ORTHO2D, data);
}

#ifndef OPENGL_ES2
static void
_draw_with_shader_cb (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (filter->default_shader);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (filter->default_shader, "tex", 1);

  gst_gl_filter_draw_texture (filter, texture);
}

/* attach target to a FBO, use shader, pass input as "tex" uniform to
 * the shader, render input to a quad */
void
gst_gl_filter_render_to_target_with_shader (GstGLFilter * filter,
    GLuint input, GLuint target, GstGLShader * shader)
{
  filter->default_shader = shader;
  gst_gl_filter_render_to_target (filter, input, target, _draw_with_shader_cb,
      filter);
}

void
gst_gl_filter_draw_texture (GstGLFilter * filter, GLuint texture)
{
  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f ((gfloat) filter->width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f ((gfloat) filter->width, (gfloat) filter->height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, (gfloat) filter->height);
  glVertex2f (-1.0, 1.0);

  glEnd ();
}
#endif
