/*
 * GStreamer
 * Copyright (C) 2010 Texas Instruments, Inc
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



/**
 * SECTION:element-basecamerasrc
 *
 * Base class for the camera src bin used by camerabin.  Indented to be
 * subclassed when plugging in more sophisticated cameras.
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstbasecamerasrc.h"

enum
{
  /* action signals */
  START_CAPTURE_SIGNAL,
  STOP_CAPTURE_SIGNAL,
  /* emit signals */
  LAST_SIGNAL
};

static guint basecamerasrc_signals[LAST_SIGNAL];

GST_DEBUG_CATEGORY (base_camera_src_debug);
#define GST_CAT_DEFAULT base_camera_src_debug

GST_BOILERPLATE (GstBaseCameraSrc, gst_base_camera_src, GstBin, GST_TYPE_BIN);

static GstStaticPadTemplate vfsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate imgsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate vidsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* note: we could provide a vmethod for derived class to overload to provide
 * it's own implementation of interface..  but in all cases I can think of at
 * moment, either the camerasrc itself, or some element within the bin, will
 * be implementing the interface..
 */

/**
 * gst_base_camera_src_get_photography:
 * @self: the camerasrc bin
 *
 * Get object implementing photography interface, if there is one.  Otherwise
 * returns NULL.
 */
GstPhotography *
gst_base_camera_src_get_photography (GstBaseCameraSrc * self)
{
  GstElement *elem;

  if (GST_IS_PHOTOGRAPHY (self)) {
    elem = GST_ELEMENT (self);
  } else {
    elem = gst_bin_get_by_interface (GST_BIN (self), GST_TYPE_PHOTOGRAPHY);
  }

  if (elem) {
    return GST_PHOTOGRAPHY (self);
  }

  return NULL;
}


/**
 * gst_base_camera_src_get_colorbalance:
 * @self: the camerasrc bin
 *
 * Get object implementing colorbalance interface, if there is one.  Otherwise
 * returns NULL.
 */
GstColorBalance *
gst_base_camera_src_get_color_balance (GstBaseCameraSrc * self)
{
  GstElement *elem;

  if (GST_IS_COLOR_BALANCE (self)) {
    elem = GST_ELEMENT (self);
  } else {
    elem = gst_bin_get_by_interface (GST_BIN (self), GST_TYPE_COLOR_BALANCE);
  }

  if (elem) {
    return GST_COLOR_BALANCE (self);
  }

  return NULL;
}

/**
 * gst_base_camera_src_set_mode:
 * @self: the camerasrc bin
 * @mode: the mode
 *
 * XXX
 */
gboolean
gst_base_camera_src_set_mode (GstBaseCameraSrc * self, GstCameraBinMode mode)
{
  GstBaseCameraSrcClass *bclass = GST_BASE_CAMERA_SRC_GET_CLASS (self);

  g_return_val_if_fail (bclass->set_mode, FALSE);

  if (bclass->set_mode (self, mode)) {
    self->mode = mode;
    return TRUE;
  }
  return FALSE;
}

/**
 * gst_base_camera_src_setup_zoom:
 * @self: camerasrc object
 *
 * Apply zoom configured to camerabin to capture.
 */
void
gst_base_camera_src_setup_zoom (GstBaseCameraSrc * self)
{
  GstBaseCameraSrcClass *bclass = GST_BASE_CAMERA_SRC_GET_CLASS (self);
  gint zoom;

  zoom = g_atomic_int_get (&self->zoom);

  g_return_if_fail (zoom);
  g_return_if_fail (bclass->set_zoom);

  bclass->set_zoom (self, zoom);
}


/**
 * gst_base_camera_src_get_allowed_input_caps:
 * @self: the camerasrc bin
 *
 * Retrieve caps from videosrc describing formats it supports
 *
 * Returns: caps object from videosrc
 */
GstCaps *
gst_base_camera_src_get_allowed_input_caps (GstBaseCameraSrc * self)
{
  GstBaseCameraSrcClass *bclass = GST_BASE_CAMERA_SRC_GET_CLASS (self);

  g_return_val_if_fail (bclass->get_allowed_input_caps, NULL);

  return bclass->get_allowed_input_caps (self);
}

/**
 * gst_base_camera_src_find_better_framerate:
 * @self: camerasrc object
 * @st: structure that contains framerate candidates
 * @orig_framerate: best framerate so far
 *
 * Looks for framerate better than @orig_framerate from @st structure.
 * In night mode lowest framerate is considered best, otherwise highest is
 * best.
 *
 * Returns: @orig_framerate or better if found
 */
const GValue *
gst_base_camera_src_find_better_framerate (GstBaseCameraSrc * self,
    GstStructure * st, const GValue * orig_framerate)
{
  const GValue *framerate = NULL;
  guint i, i_best, list_size;
  gint res, comparison;

  if (self->night_mode) {
    GST_LOG_OBJECT (self, "finding min framerate in %" GST_PTR_FORMAT, st);
    comparison = GST_VALUE_LESS_THAN;
  } else {
    GST_LOG_OBJECT (self, "finding max framerate in %" GST_PTR_FORMAT, st);
    comparison = GST_VALUE_GREATER_THAN;
  }

  if (gst_structure_has_field (st, "framerate")) {
    framerate = gst_structure_get_value (st, "framerate");
    /* Handle framerate lists */
    if (GST_VALUE_HOLDS_LIST (framerate)) {
      list_size = gst_value_list_get_size (framerate);
      GST_LOG_OBJECT (self, "finding framerate from list");
      for (i = 0, i_best = 0; i < list_size; i++) {
        res = gst_value_compare (gst_value_list_get_value (framerate, i),
            gst_value_list_get_value (framerate, i_best));
        if (comparison == res) {
          i_best = i;
        }
      }
      GST_LOG_OBJECT (self, "found best framerate from index %d", i_best);
      framerate = gst_value_list_get_value (framerate, i_best);
    }
    /* Handle framerate ranges */
    if (GST_VALUE_HOLDS_FRACTION_RANGE (framerate)) {
      if (self->night_mode) {
        GST_LOG_OBJECT (self, "getting min framerate from range");
        framerate = gst_value_get_fraction_range_min (framerate);
      } else {
        GST_LOG_OBJECT (self, "getting max framerate from range");
        framerate = gst_value_get_fraction_range_max (framerate);
      }
    }
  }

  /* Check if we found better framerate */
  if (orig_framerate && framerate) {
    res = gst_value_compare (orig_framerate, framerate);
    if (comparison == res) {
      GST_LOG_OBJECT (self, "original framerate was the best");
      framerate = orig_framerate;
    }
  }

  return framerate;
}

static void
gst_base_camera_src_start_capture (GstBaseCameraSrc * src)
{
  GstBaseCameraSrcClass *klass = GST_BASE_CAMERA_SRC_GET_CLASS (src);

  g_return_if_fail (klass->start_capture != NULL);

  g_mutex_lock (src->capturing_mutex);
  if (src->capturing) {
    GST_WARNING_OBJECT (src, "Capturing already ongoing");
    g_mutex_unlock (src->capturing_mutex);
    return;
  }

  if (klass->start_capture (src)) {
    src->capturing = TRUE;
    g_object_notify (G_OBJECT (src), "ready-for-capture");
  } else {
    GST_WARNING_OBJECT (src, "Failed to start capture");
  }
  g_mutex_unlock (src->capturing_mutex);
}

static void
gst_base_camera_src_stop_capture (GstBaseCameraSrc * src)
{
  GstBaseCameraSrcClass *klass = GST_BASE_CAMERA_SRC_GET_CLASS (src);

  g_return_if_fail (klass->stop_capture != NULL);

  g_mutex_lock (src->capturing_mutex);
  if (!src->capturing) {
    GST_DEBUG_OBJECT (src, "No ongoing capture");
    g_mutex_unlock (src->capturing_mutex);
    return;
  }
  klass->stop_capture (src);
  g_mutex_unlock (src->capturing_mutex);
}

void
gst_base_camera_src_finish_capture (GstBaseCameraSrc * self)
{
  g_return_if_fail (self->capturing);
  self->capturing = FALSE;
  g_object_notify (G_OBJECT (self), "ready-for-capture");
}


/**
 *
 */
static void
gst_base_camera_src_dispose (GObject * object)
{
  GstBaseCameraSrc *src = GST_BASE_CAMERA_SRC_CAST (object);

  g_mutex_free (src->capturing_mutex);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_base_camera_src_finalize (GstBaseCameraSrc * self)
{
  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (self));
}

static void
gst_base_camera_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstBaseCameraSrc *self = GST_BASE_CAMERA_SRC (object);

  switch (prop_id) {
    case ARG_MODE:
      gst_base_camera_src_set_mode (GST_BASE_CAMERA_SRC (self),
          g_value_get_enum (value));
      break;
    case ARG_ZOOM:{
      g_atomic_int_set (&self->zoom, g_value_get_int (value));
      /* does not set it if in NULL, the src is not created yet */
      if (GST_STATE (self) != GST_STATE_NULL)
        gst_base_camera_src_setup_zoom (self);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_base_camera_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstBaseCameraSrc *self = GST_BASE_CAMERA_SRC (object);

  switch (prop_id) {
    case ARG_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case ARG_READY_FOR_CAPTURE:
      g_value_set_boolean (value, !self->capturing);
      break;
    case ARG_ZOOM:
      g_value_set_int (value, g_atomic_int_get (&self->zoom));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static gboolean
construct_pipeline (GstBaseCameraSrc * self)
{
  GstBaseCameraSrcClass *bclass = GST_BASE_CAMERA_SRC_GET_CLASS (self);

  g_return_val_if_fail (bclass->construct_pipeline, FALSE);

  if (!bclass->construct_pipeline (self)) {
    GST_ERROR_OBJECT (self, "pipeline construction failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
setup_pipeline (GstBaseCameraSrc * self)
{
  GstBaseCameraSrcClass *bclass = GST_BASE_CAMERA_SRC_GET_CLASS (self);
  if (bclass->setup_pipeline)
    return bclass->setup_pipeline (self);
  return TRUE;
}

static GstStateChangeReturn
gst_base_camera_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseCameraSrc *self = GST_BASE_CAMERA_SRC (element);

  GST_DEBUG_OBJECT (self, "%d -> %d",
      GST_STATE_TRANSITION_CURRENT (transition),
      GST_STATE_TRANSITION_NEXT (transition));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!construct_pipeline (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!setup_pipeline (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_base_camera_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (base_camera_src_debug, "base_camera_src", 0,
      "Base camera src");

  gst_element_class_set_details_simple (gstelement_class,
      "Base class for camerabin src bin", "Source/Video",
      "Abstracts capture device for camerabin2", "Rob Clark <rob@ti.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vfsrc_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&imgsrc_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vidsrc_template));
}

static void
gst_base_camera_src_class_init (GstBaseCameraSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = gst_base_camera_src_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_base_camera_src_finalize;
  gobject_class->set_property = gst_base_camera_src_set_property;
  gobject_class->get_property = gst_base_camera_src_get_property;

  // g_object_class_install_property ....
  g_object_class_install_property (gobject_class, ARG_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The capture mode (still image capture or video recording)",
          GST_TYPE_CAMERABIN_MODE, MODE_IMAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstBaseCameraSrc:ready-for-capture:
   *
   * When TRUE new capture can be prepared. If FALSE capturing is ongoing
   * and starting a new capture immediately is not possible.
   *
   * Note that calling start-capture from the notify callback of this property
   * will cause a deadlock. If you need to react like this on the notify
   * function, please schedule a new thread to do it. If you're using glib's
   * mainloop you can use g_idle_add() for example.
   */
  g_object_class_install_property (gobject_class, ARG_READY_FOR_CAPTURE,
      g_param_spec_boolean ("ready-for-capture", "Ready for capture",
          "Informs this element is ready for starting another capture",
          TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));


  /* Signals */
  basecamerasrc_signals[START_CAPTURE_SIGNAL] =
      g_signal_new ("start-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstBaseCameraSrcClass, private_start_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  basecamerasrc_signals[STOP_CAPTURE_SIGNAL] =
      g_signal_new ("stop-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstBaseCameraSrcClass, private_stop_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /* TODO these should be moved to a private struct
   * that is allocated sequentially to the main struct as said at:
   * http://library.gnome.org/devel/gobject/unstable/gobject-Type-Information.html#g-type-add-class-private
   */
  klass->private_start_capture = gst_base_camera_src_start_capture;
  klass->private_stop_capture = gst_base_camera_src_stop_capture;

  gstelement_class->change_state = gst_base_camera_src_change_state;
}

static void
gst_base_camera_src_init (GstBaseCameraSrc * self,
    GstBaseCameraSrcClass * klass)
{
  self->width = DEFAULT_WIDTH;
  self->height = DEFAULT_HEIGHT;
  self->zoom = DEFAULT_ZOOM;
  self->image_capture_width = 0;
  self->image_capture_height = 0;
  self->mode = MODE_IMAGE;

  self->night_mode = FALSE;

  self->fps_n = DEFAULT_FPS_N;
  self->fps_d = DEFAULT_FPS_D;

  self->capturing = FALSE;
  self->capturing_mutex = g_mutex_new ();
}
