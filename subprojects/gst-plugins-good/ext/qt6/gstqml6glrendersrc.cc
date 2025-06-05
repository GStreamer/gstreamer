/*
 * GStreamer
 * Copyright (C) 2025 Matthew Waters <matthew@centricular.com>
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
 * SECTION:element-qml6glrendersrc
 *
 * A video src that renders a QML scene and produces video buffers.
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqt6elements.h"
#include "gstqml6glrendersrc.h"
#include "gstqt6glutility.h"
#include <QtGui/QGuiApplication>

#define GST_CAT_DEFAULT gst_debug_qml6_gl_render_src
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define DEFAULT_IS_LIVE FALSE

static void gst_qml6_gl_render_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qml6_gl_render_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_qml6_gl_render_src_finalize (GObject * object);

static gboolean gst_qml6_gl_render_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps);
static GstCaps *gst_qml6_gl_render_src_fixate (GstBaseSrc * bsrc, GstCaps * caps);
static GstStateChangeReturn gst_qml6_gl_render_src_change_state (GstElement * element,
    GstStateChange transition);
static void gst_qml6_gl_render_src_gl_stop (GstGLBaseSrc * basesrc);
static gboolean gst_qml6_gl_render_src_fill_gl_memory (GstGLBaseSrc * basesrc, GstGLMemory * gl_mem);

static GstStaticPadTemplate gst_qml6_gl_render_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
        "format = (string) RGBA, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "texture-target = (string) 2D"));

enum
{
  PROP_0,
  PROP_ROOT_ITEM,
  PROP_QML_SCENE,
};

enum
{
  SIGNAL_0,
  SIGNAL_QML_SCENE_INITIALIZED,
  SIGNAL_QML_SCENE_DESTROYED,
  LAST_SIGNAL
};

static guint gst_qml6_gl_render_src_signals[LAST_SIGNAL] = { 0 };

#define gst_qml6_gl_render_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQml6GLRenderSrc, gst_qml6_gl_render_src,
    GST_TYPE_GL_BASE_SRC, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "qml6glrendersrc", 0, "Qt6 Qml Render Video Src"));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (qml6glrendersrc, "qml6glrendersrc",
    GST_RANK_NONE, GST_TYPE_QML6_GL_RENDER_SRC, qt6_element_init (plugin));

static void
gst_qml6_gl_render_src_class_init (GstQml6GLRenderSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseSrcClass *gstbasesrc_class = (GstBaseSrcClass *) klass;
  GstGLBaseSrcClass *gstglbasesrc_class = (GstGLBaseSrcClass *) klass;

  gobject_class->set_property = gst_qml6_gl_render_src_set_property;
  gobject_class->get_property = gst_qml6_gl_render_src_get_property;
  gobject_class->finalize = gst_qml6_gl_render_src_finalize;

  gst_element_class_set_static_metadata (gstelement_class, "Qt6 Render Video Source",
      "Source/Video", "A video src that renders a QML scene",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_qml6_gl_render_src_template));

  g_object_class_install_property (gobject_class, PROP_ROOT_ITEM,
      g_param_spec_pointer ("root-item", "QQuickItem",
          "Provides the 'QQuickItem *' to render.",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_QML_SCENE,
      g_param_spec_string ("qml-scene", "QML Scene",
          "The contents of the QML scene", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstQmlGLOverlay::qml-scene-initialized
   * @element: the #GstQmlGLOverlay
   * @user_data: user provided data
   */
  gst_qml6_gl_render_src_signals[SIGNAL_QML_SCENE_INITIALIZED] =
      g_signal_new ("qml-scene-initialized", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstQmlGLOverlay::qml-scene-destroyed
   * @element: the #GstQmlGLOverlay
   * @user_data: user provided data
   */
  gst_qml6_gl_render_src_signals[SIGNAL_QML_SCENE_DESTROYED] =
      g_signal_new ("qml-scene-destroyed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  gstelement_class->change_state = gst_qml6_gl_render_src_change_state;

  gstbasesrc_class->set_caps = gst_qml6_gl_render_src_set_caps;
  gstbasesrc_class->fixate = gst_qml6_gl_render_src_fixate;
  gstglbasesrc_class->gl_stop = gst_qml6_gl_render_src_gl_stop;
  gstglbasesrc_class->fill_gl_memory = gst_qml6_gl_render_src_fill_gl_memory;
}

static void
gst_qml6_gl_render_src_init (GstQml6GLRenderSrc * src)
{
  GstGLBaseSrc *glbase_src = GST_GL_BASE_SRC (src);

  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), DEFAULT_IS_LIVE);

  src->renderer = new GstQt6QuickRenderer;
  glbase_src->display = gst_qml6_get_gl_display (FALSE);
  if (glbase_src->display)
    src->display = (GstGLDisplay *) gst_object_ref (glbase_src->display);
}

static void
gst_qml6_gl_render_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQml6GLRenderSrc *src = GST_QML6_GL_RENDER_SRC (object);

  switch (prop_id) {
    case PROP_ROOT_ITEM:
      GST_OBJECT_LOCK (src);
      src->root_item = (QQuickItem *) g_value_get_pointer (value);
      GST_OBJECT_UNLOCK (src);
      break;
    case PROP_QML_SCENE:
      GST_OBJECT_LOCK (src);
      src->qml_scene = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qml6_gl_render_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQml6GLRenderSrc *src = GST_QML6_GL_RENDER_SRC (object);

  switch (prop_id) {
    case PROP_ROOT_ITEM: {
      GST_OBJECT_LOCK (src);
      QQuickItem *root = src->renderer->rootItem();
      if (root)
        g_value_set_pointer (value, root);
      else
        g_value_set_pointer (value, src->root_item);
      GST_OBJECT_UNLOCK (src);
      break;
    }
    case PROP_QML_SCENE:
      GST_OBJECT_LOCK (src);
      g_value_set_string (value, src->qml_scene);
      GST_OBJECT_UNLOCK (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qml6_gl_render_src_finalize (GObject * object)
{
  GstQml6GLRenderSrc *qt_src = GST_QML6_GL_RENDER_SRC (object);
  GstGLBaseSrc *glbase_src = GST_GL_BASE_SRC (object);

  GST_DEBUG ("qmlglrendersrc finalize");

  if (qt_src->renderer)
    delete qt_src->renderer;

  gst_clear_object (&glbase_src->display);
  gst_clear_object (&qt_src->display);

  g_free (qt_src->qml_scene);
  qt_src->qml_scene = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_qml6_gl_render_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstQml6GLRenderSrc *src = GST_QML6_GL_RENDER_SRC (bsrc);
  GstGLBaseSrc *gl_src = GST_GL_BASE_SRC (bsrc);

  if (!GST_BASE_SRC_CLASS (parent_class)->set_caps (bsrc, caps))
    return FALSE;

  if (src->renderer)
    src->renderer->setSize (GST_VIDEO_INFO_WIDTH (&gl_src->out_info), GST_VIDEO_INFO_HEIGHT (&gl_src->out_info));

  return TRUE;
}

static GstCaps *
gst_qml6_gl_render_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *structure;

  GST_DEBUG ("fixate");

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

  return caps;
}

static void
gst_qml6_gl_render_src_gl_stop (GstGLBaseSrc * glbase_src)
{
  GstQml6GLRenderSrc *src = GST_QML6_GL_RENDER_SRC (glbase_src);
  GstQt6QuickRenderer *renderer = NULL;

  /* notify before actually destroying anything */
  GST_OBJECT_LOCK (src);
  if (src->renderer) {
    renderer = src->renderer;
    src->renderer = new GstQt6QuickRenderer;
  }
  src->initted = FALSE;
  GST_OBJECT_UNLOCK (src);

  g_signal_emit (src, gst_qml6_gl_render_src_signals[SIGNAL_QML_SCENE_DESTROYED], 0);
  g_object_notify (G_OBJECT (src), "root-item");

  if (renderer) {
    renderer->cleanup();
    delete renderer;
  }

  GST_GL_BASE_SRC_CLASS (parent_class)->gl_stop (glbase_src);
}

static gboolean
gst_qml6_gl_render_src_fill_gl_memory (GstGLBaseSrc * glbase_src, GstGLMemory * gl_mem)
{
  GstQml6GLRenderSrc *src = GST_QML6_GL_RENDER_SRC (glbase_src);
  gboolean have_scene;
  GError *error = NULL;

  have_scene = src->qml_scene && g_strcmp0 (src->qml_scene, "") != 0;
  if (!have_scene && !src->root_item) {
    GST_ELEMENT_ERROR (glbase_src, RESOURCE, NOT_FOUND, ("root-item property not set"), (NULL));
    return FALSE;
  }

  if (!src->initted) {
    GST_OBJECT_LOCK (glbase_src);
    if (!src->renderer->init (glbase_src->context, &error)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (glbase_src), RESOURCE, NOT_FOUND,
          ("%s", error->message), (NULL));
      delete src->renderer;
      src->renderer = new GstQt6QuickRenderer;
      GST_OBJECT_UNLOCK (glbase_src);
      return FALSE;
    }

    /* FIXME: Qml may do async loading and we need to propagate qml errors in that case as well */
    if (have_scene) {
      if (!src->renderer->setQmlScene (src->qml_scene, &error)) {
        GST_ELEMENT_ERROR (GST_ELEMENT (glbase_src), RESOURCE, NOT_FOUND,
            ("%s", error->message), (NULL));
        goto fail_renderer;
      }

      QQuickItem *root = src->renderer->rootItem();
      if (!root) {
        GST_ELEMENT_ERROR (GST_ELEMENT (glbase_src), RESOURCE, NOT_FOUND,
            ("Qml scene does not have a root item"), (NULL));
        goto fail_renderer;
      }
      src->renderer->setSize (GST_VIDEO_INFO_WIDTH (&glbase_src->out_info), GST_VIDEO_INFO_HEIGHT (&glbase_src->out_info));
      GST_OBJECT_UNLOCK (glbase_src);

      g_object_notify (G_OBJECT (src), "root-item");
    } else {
      if (!src->root_item) {
        GST_ELEMENT_ERROR (GST_ELEMENT (glbase_src), RESOURCE, NOT_FOUND,
            ("No root item provided"), (NULL));
        goto fail_renderer;
      }
      src->renderer->setRootItem(src->root_item);
      src->renderer->setSize (GST_VIDEO_INFO_WIDTH (&glbase_src->out_info), GST_VIDEO_INFO_HEIGHT (&glbase_src->out_info));
      GST_OBJECT_UNLOCK (glbase_src);
    }

    g_signal_emit (src, gst_qml6_gl_render_src_signals[SIGNAL_QML_SCENE_INITIALIZED], 0);

    src->initted = TRUE;
  }

  if (!src->renderer->generateInto (glbase_src->running_time, gl_mem)) {
    GST_ERROR_OBJECT (glbase_src, "Failed to generate output");
    return FALSE;
  }

  return TRUE;

fail_renderer:
  {
    src->renderer->cleanup();
    delete src->renderer;
    src->renderer = new GstQt6QuickRenderer;
    GST_OBJECT_UNLOCK (glbase_src);
    return FALSE;
  }
}

static GstStateChangeReturn
gst_qml6_gl_render_src_change_state (GstElement * element, GstStateChange transition)
{
  GstGLBaseSrc *glbase_src = GST_GL_BASE_SRC (element);
  GstQml6GLRenderSrc *src = GST_QML6_GL_RENDER_SRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  QGuiApplication *app;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY: {
      app = static_cast < QGuiApplication * >(QCoreApplication::instance ());
      if (!app) {
        GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
            ("%s", "Failed to connect to Qt"),
            ("%s", "Could not retrieve QGuiApplication instance"));
        return GST_STATE_CHANGE_FAILURE;
      }
      gst_gl_element_propagate_display_context (element, src->display);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (src->display)
        glbase_src->display = (GstGLDisplay *) gst_object_ref (src->display);
      break;
    default:
      break;
  }

  return ret;
}
