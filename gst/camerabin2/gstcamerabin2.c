/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
 * SECTION:element-gstcamerabin2
 *
 * The gstcamerabin2 element does FIXME stuff.
 *
 * Note that camerabin2 is still UNSTABLE, EXPERIMENTAL and under heavy
 * development.
 */

/*
 * Detail Topics:
 *
 * videorecordingbin state management (for now on videobin)
 * - The problem: keeping videobin state in sync with camerabin will make it
 *                go to playing when it might not be used, causing its internal
 *                filesink to open a file that might be left blank.
 * - The solution: videobin state is set to locked upon its creation and camerabin
 *                 registers itself on the notify::ready-for-capture of the src.
 *                 Whenever the src readyness goes to FALSE it means a new
 *                 capture is starting. If we are on video mode, the videobin's
 *                 state is set to NULL and then PLAYING (in between this we
 *                 have room to set the destination filename).
 *                 There is no problem to leave it on playing after an EOS, so
 *                 no action is taken on stop-capture.
 * - TODO: What happens when an error pops?
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#include "gstcamerabin2.h"

GST_DEBUG_CATEGORY_STATIC (gst_camera_bin_debug);
#define GST_CAT_DEFAULT gst_camera_bin_debug

/* prototypes */

enum
{
  PROP_0,
  PROP_MODE,
  PROP_LOCATION,
  PROP_CAMERA_SRC,
  PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
  PROP_VIDEO_CAPTURE_SUPPORTED_CAPS,
  PROP_IMAGE_CAPTURE_CAPS,
  PROP_VIDEO_CAPTURE_CAPS
};

enum
{
  /* action signals */
  START_CAPTURE_SIGNAL,
  STOP_CAPTURE_SIGNAL,
  /* emit signals */
  LAST_SIGNAL
};
static guint camerabin_signals[LAST_SIGNAL];

#define DEFAULT_MODE MODE_IMAGE
#define DEFAULT_VID_LOCATION "vid_%d"
#define DEFAULT_IMG_LOCATION "img_%d"

/********************************
 * Standard GObject boilerplate *
 * and GObject types            *
 ********************************/

static GstPipelineClass *parent_class;
static void gst_camera_bin_class_init (GstCameraBinClass * klass);
static void gst_camera_bin_base_init (gpointer klass);
static void gst_camera_bin_init (GstCameraBin * camera);
static void gst_camera_bin_dispose (GObject * object);
static void gst_camera_bin_finalize (GObject * object);

GType
gst_camera_bin_get_type (void)
{
  static GType gst_camera_bin_type = 0;

  if (!gst_camera_bin_type) {
    static const GTypeInfo gst_camera_bin_info = {
      sizeof (GstCameraBinClass),
      (GBaseInitFunc) gst_camera_bin_base_init,
      NULL,
      (GClassInitFunc) gst_camera_bin_class_init,
      NULL,
      NULL,
      sizeof (GstCameraBin),
      0,
      (GInstanceInitFunc) gst_camera_bin_init,
      NULL
    };

    gst_camera_bin_type =
        g_type_register_static (GST_TYPE_PIPELINE, "GstCameraBin2",
        &gst_camera_bin_info, 0);
  }

  return gst_camera_bin_type;
}

/* GObject class functions */
static void gst_camera_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_camera_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* Element class functions */
static GstStateChangeReturn
gst_camera_bin_change_state (GstElement * element, GstStateChange trans);


/* Camerabin functions */

static GstEvent *
gst_camera_bin_new_event_renegotiate (void)
{
  return gst_event_new_custom (GST_EVENT_CUSTOM_BOTH,
      gst_structure_new ("renegotiate", NULL));
}

static void
gst_camera_bin_start_capture (GstCameraBin * camerabin)
{
  GST_DEBUG_OBJECT (camerabin, "Received start-capture");
  g_signal_emit_by_name (camerabin->src, "start-capture", NULL);
}

static void
gst_camera_bin_stop_capture (GstCameraBin * camerabin)
{
  if (camerabin->src)
    g_signal_emit_by_name (camerabin->src, "stop-capture", NULL);
}

static void
gst_camera_bin_change_mode (GstCameraBin * camerabin, gint mode)
{
  if (mode == camerabin->mode)
    return;

  GST_DEBUG_OBJECT (camerabin, "Changing mode to %d", mode);

  /* stop any ongoing capture */
  gst_camera_bin_stop_capture (camerabin);
  camerabin->mode = mode;
  if (camerabin->src)
    g_object_set (camerabin->src, "mode", mode, NULL);
}

static void
gst_camera_bin_src_notify_readyforcapture (GObject * obj, GParamSpec * pspec,
    gpointer user_data)
{
  GstCameraBin *camera = GST_CAMERA_BIN_CAST (user_data);
  gboolean ready;

  if (camera->mode == MODE_VIDEO) {
    g_object_get (camera->src, "ready-for-capture", &ready, NULL);
    if (!ready) {
      gchar *location;

      /* a video recording is about to start, we reset the videobin to clear eos/flushing state
       * also need to clean the queue ! capsfilter before it */
      gst_element_set_state (camera->videobin, GST_STATE_NULL);
      gst_element_set_state (camera->videobin_queue, GST_STATE_NULL);
      gst_element_set_state (camera->videobin_capsfilter, GST_STATE_NULL);
      location =
          g_strdup_printf (camera->video_location, camera->video_index++);
      GST_DEBUG_OBJECT (camera, "Switching videobin location to %s", location);
      g_object_set (camera->videobin, "location", location, NULL);
      g_free (location);
      gst_element_set_state (camera->videobin, GST_STATE_PLAYING);
      gst_element_set_state (camera->videobin_capsfilter, GST_STATE_PLAYING);
      gst_element_set_state (camera->videobin_queue, GST_STATE_PLAYING);
    }
  }
}

static void
gst_camera_bin_dispose (GObject * object)
{
  GstCameraBin *camerabin = GST_CAMERA_BIN_CAST (object);

  g_free (camerabin->image_location);
  g_free (camerabin->video_location);

  if (camerabin->src_capture_notify_id)
    g_signal_handler_disconnect (camerabin->src,
        camerabin->src_capture_notify_id);
  if (camerabin->src)
    gst_object_unref (camerabin->src);
  if (camerabin->user_src)
    gst_object_unref (camerabin->user_src);

  if (camerabin->viewfinderbin)
    gst_object_unref (camerabin->viewfinderbin);
  if (camerabin->viewfinderbin_queue)
    gst_object_unref (camerabin->viewfinderbin_queue);
  if (camerabin->viewfinderbin_capsfilter)
    gst_object_unref (camerabin->viewfinderbin_capsfilter);


  if (camerabin->videobin)
    gst_object_unref (camerabin->videobin);
  if (camerabin->videobin_queue)
    gst_object_unref (camerabin->videobin_queue);
  if (camerabin->videobin_capsfilter)
    gst_object_unref (camerabin->videobin_capsfilter);

  if (camerabin->imagebin)
    gst_object_unref (camerabin->imagebin);
  if (camerabin->imagebin_queue)
    gst_object_unref (camerabin->imagebin_queue);
  if (camerabin->imagebin_capsfilter)
    gst_object_unref (camerabin->imagebin_capsfilter);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_camera_bin_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_camera_bin_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "CameraBin2",
      "Generic/Bin/Camera", "CameraBin2",
      "Thiago Santos <thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_camera_bin_class_init (GstCameraBinClass * klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;

  parent_class = g_type_class_peek_parent (klass);
  object_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  object_class->dispose = gst_camera_bin_dispose;
  object_class->finalize = gst_camera_bin_finalize;
  object_class->set_property = gst_camera_bin_set_property;
  object_class->get_property = gst_camera_bin_get_property;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_camera_bin_change_state);

  klass->start_capture = gst_camera_bin_start_capture;
  klass->stop_capture = gst_camera_bin_stop_capture;

  /**
   * GstCameraBin:mode:
   *
   * Set the mode of operation: still image capturing or video recording.
   */
  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The capture mode (still image capture or video recording)",
          GST_TYPE_CAMERABIN_MODE, DEFAULT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to save the captured files. A %d might be used on the"
          "filename as a placeholder for a numeric index of the capture."
          "Default for images is img_%d and vid_%d for videos",
          DEFAULT_IMG_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CAMERA_SRC,
      g_param_spec_object ("camera-src", "Camera source",
          "The camera source element to be used",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
      g_param_spec_boxed ("image-capture-supported-caps",
          "Image capture supported caps",
          "Formats supported for capturing images represented as GstCaps",
          GST_TYPE_CAPS, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_VIDEO_CAPTURE_SUPPORTED_CAPS,
      g_param_spec_boxed ("video-capture-supported-caps",
          "Video capture supported caps",
          "Formats supported for capturing videos represented as GstCaps",
          GST_TYPE_CAPS, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_IMAGE_CAPTURE_CAPS,
      g_param_spec_boxed ("image-capture-caps",
          "Image capture caps",
          "Caps for image capture",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_VIDEO_CAPTURE_CAPS,
      g_param_spec_boxed ("video-capture-caps",
          "Video capture caps",
          "Caps for video capture",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  /**
   * GstCameraBin::capture-start:
   * @camera: the camera bin element
   *
   * Starts image capture or video recording depending on the Mode.
   */
  camerabin_signals[START_CAPTURE_SIGNAL] =
      g_signal_new ("start-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, start_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::capture-stop:
   * @camera: the camera bin element
   */
  camerabin_signals[STOP_CAPTURE_SIGNAL] =
      g_signal_new ("stop-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, stop_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
gst_camera_bin_init (GstCameraBin * camera)
{
  camera->mode = DEFAULT_MODE;
  camera->video_location = g_strdup (DEFAULT_VID_LOCATION);
  camera->image_location = g_strdup (DEFAULT_IMG_LOCATION);
  camera->viewfinderbin = gst_element_factory_make ("viewfinderbin", "vf-bin");

  /* capsfilters are created here as we proxy their caps properties and
   * this way we avoid having to store the caps while on NULL state to 
   * set them later */
  camera->videobin_capsfilter = gst_element_factory_make ("capsfilter",
      "videobin-capsfilter");
  camera->imagebin_capsfilter = gst_element_factory_make ("capsfilter",
      "imagebin-capsfilter");
  camera->viewfinderbin_capsfilter = gst_element_factory_make ("capsfilter",
      "viewfinderbin-capsfilter");

  gst_bin_add_many (GST_BIN (camera),
      gst_object_ref (camera->viewfinderbin),
      gst_object_ref (camera->videobin_capsfilter),
      gst_object_ref (camera->imagebin_capsfilter),
      gst_object_ref (camera->viewfinderbin_capsfilter), NULL);
}

/**
 * gst_camera_bin_create_elements:
 * @param camera: the #GstCameraBin
 *
 * Creates all elements inside #GstCameraBin
 *
 * Each of the pads on the camera source is linked as follows:
 * .pad ! queue ! capsfilter ! correspondingbin
 *
 * Where 'correspondingbin' is the bin appropriate for
 * the camera source pad.
 */
static gboolean
gst_camera_bin_create_elements (GstCameraBin * camera)
{
  gboolean new_src = FALSE;

  if (!camera->elements_created) {

    camera->videobin =
        gst_element_factory_make ("videorecordingbin", "videobin");
    camera->imagebin = gst_element_factory_make ("imagecapturebin", "imagebin");

    camera->videobin_queue =
        gst_element_factory_make ("queue", "videobin-queue");
    camera->imagebin_queue =
        gst_element_factory_make ("queue", "imagebin-queue");
    camera->viewfinderbin_queue =
        gst_element_factory_make ("queue", "viewfinderbin-queue");

    gst_bin_add_many (GST_BIN_CAST (camera),
        gst_object_ref (camera->videobin), gst_object_ref (camera->imagebin),
        gst_object_ref (camera->videobin_queue),
        gst_object_ref (camera->imagebin_queue),
        gst_object_ref (camera->viewfinderbin_queue), NULL);

    /* Linking can be optimized TODO */
    gst_element_link_many (camera->videobin_queue, camera->videobin_capsfilter,
        camera->videobin, NULL);
    gst_element_link_many (camera->imagebin_queue, camera->imagebin_capsfilter,
        camera->imagebin, NULL);
    gst_element_link_many (camera->viewfinderbin_queue,
        camera->viewfinderbin_capsfilter, camera->viewfinderbin, NULL);
    /*
     * Video can't get into playing as its internal filesink will open
     * a file for writing and leave it empty if unused.
     *
     * Its state is managed using the current mode and the source's
     * ready-for-capture notify callback. When we are at video mode and
     * the source's ready-for-capture goes to FALSE it means it is
     * starting recording, so we should prepare the video bin.
     */
    gst_element_set_locked_state (camera->videobin, TRUE);

    g_object_set (camera->videobin, "location", camera->video_location, NULL);
    g_object_set (camera->imagebin, "location", camera->image_location, NULL);
  }

  /* check if we need to replace the camera src */

  if (camera->src) {
    if (camera->user_src && camera->user_src != camera->src) {

      if (camera->src_capture_notify_id)
        g_signal_handler_disconnect (camera->src,
            camera->src_capture_notify_id);

      gst_bin_remove (GST_BIN_CAST (camera), camera->src);
      gst_object_unref (camera->src);
      camera->src = NULL;
    }
  }

  if (!camera->src) {
    if (camera->user_src) {
      camera->src = gst_object_ref (camera->user_src);
    } else {
      camera->src =
          gst_element_factory_make ("wrappercamerabinsrc", "camerasrc");
    }

    new_src = TRUE;
  }

  g_assert (camera->src != NULL);
  g_object_set (camera->src, "mode", camera->mode, NULL);
  if (new_src) {
    gst_bin_add (GST_BIN_CAST (camera), gst_object_ref (camera->src));
    camera->src_capture_notify_id = g_signal_connect (G_OBJECT (camera->src),
        "notify::ready-for-capture",
        G_CALLBACK (gst_camera_bin_src_notify_readyforcapture), camera);
    gst_element_link_pads (camera->src, "vfsrc", camera->viewfinderbin_queue,
        "sink");
    gst_element_link_pads (camera->src, "imgsrc", camera->imagebin_queue,
        "sink");
    gst_element_link_pads (camera->src, "vidsrc", camera->videobin_queue,
        "sink");
  }

  camera->elements_created = TRUE;
  return TRUE;
}

static GstStateChangeReturn
gst_camera_bin_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCameraBin *camera = GST_CAMERA_BIN_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_camera_bin_create_elements (camera)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  switch (trans) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_element_set_state (camera->videobin, GST_STATE_NULL);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_camera_bin_set_location (GstCameraBin * camera, const gchar * location)
{
  GST_DEBUG_OBJECT (camera, "Setting mode %d location to %s", camera->mode,
      location);
  if (camera->mode == MODE_IMAGE) {
    if (camera->imagebin)
      g_object_set (camera->imagebin, "location", location, NULL);
    g_free (camera->image_location);
    camera->image_location = g_strdup (location);
  } else {
    g_free (camera->video_location);
    camera->video_location = g_strdup (location);
  }
}

static void
gst_camera_bin_set_camera_src (GstCameraBin * camera, GstElement * src)
{
  GST_DEBUG_OBJECT (GST_OBJECT (camera),
      "Setting camera source %" GST_PTR_FORMAT, src);

  if (camera->user_src)
    g_object_unref (camera->user_src);

  if (src)
    g_object_ref (src);
  camera->user_src = src;
}

static void
gst_camera_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCameraBin *camera = GST_CAMERA_BIN_CAST (object);

  switch (prop_id) {
    case PROP_MODE:
      gst_camera_bin_change_mode (camera, g_value_get_enum (value));
      break;
    case PROP_LOCATION:
      gst_camera_bin_set_location (camera, g_value_get_string (value));
      break;
    case PROP_CAMERA_SRC:
      gst_camera_bin_set_camera_src (camera, g_value_get_object (value));
      break;
    case PROP_IMAGE_CAPTURE_CAPS:{
      GstPad *pad = NULL;

      if (camera->src)
        pad =
            gst_element_get_static_pad (camera->src,
            GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME);

      GST_DEBUG_OBJECT (camera,
          "Setting image capture caps to %" GST_PTR_FORMAT,
          gst_value_get_caps (value));

      /* set the capsfilter caps and notify the src to renegotiate */
      g_object_set (camera->imagebin_capsfilter, "caps",
          gst_value_get_caps (value), NULL);
      if (pad) {
        GST_DEBUG_OBJECT (camera, "Pushing renegotiate on %s",
            GST_PAD_NAME (pad));
        GST_PAD_EVENTFUNC (pad) (pad, gst_camera_bin_new_event_renegotiate ());
        gst_object_unref (pad);
      }
    }
      break;
    case PROP_VIDEO_CAPTURE_CAPS:{
      GstPad *pad = NULL;

      if (camera->src)
        pad =
            gst_element_get_static_pad (camera->src,
            GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME);

      GST_DEBUG_OBJECT (camera,
          "Setting video capture caps to %" GST_PTR_FORMAT,
          gst_value_get_caps (value));

      /* set the capsfilter caps and notify the src to renegotiate */
      g_object_set (camera->videobin_capsfilter, "caps",
          gst_value_get_caps (value), NULL);
      if (pad) {
        GST_DEBUG_OBJECT (camera, "Pushing renegotiate on %s",
            GST_PAD_NAME (pad));
        gst_pad_push_event (pad, gst_camera_bin_new_event_renegotiate ());
        gst_object_unref (pad);
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camera_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraBin *camera = GST_CAMERA_BIN_CAST (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, camera->mode);
      break;
    case PROP_LOCATION:
      if (camera->mode == MODE_VIDEO) {
        g_value_set_string (value, camera->video_location);
      } else {
        g_value_set_string (value, camera->image_location);
      }
      break;
    case PROP_CAMERA_SRC:
      g_value_set_object (value, camera->src);
      break;
    case PROP_VIDEO_CAPTURE_SUPPORTED_CAPS:
    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:{
      GstPad *pad;
      GstCaps *caps;
      const gchar *padname;

      if (prop_id == PROP_VIDEO_CAPTURE_SUPPORTED_CAPS) {
        padname = GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME;
      } else {
        padname = GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME;
      }

      if (camera->src) {
        pad = gst_element_get_static_pad (camera->src, padname);

        g_assert (pad != NULL);

        /* TODO not sure if we want get_caps or get_allowed_caps to already
         * consider the full pipeline scenario and avoid picking a caps that
         * won't negotiate. Need to take care on the special case of the
         * pad being unlinked.
         */
        caps = gst_pad_get_caps_reffed (pad);
        if (caps) {
          gst_value_set_caps (value, caps);
          gst_caps_unref (caps);
        }

        gst_object_unref (pad);
      } else {
        GST_DEBUG_OBJECT (camera, "Camera source not created, can't get "
            "supported caps");
      }
    }
      break;
    case PROP_IMAGE_CAPTURE_CAPS:{
      GstCaps *caps = NULL;
      g_object_get (camera->imagebin_capsfilter, "caps", &caps, NULL);
      gst_value_set_caps (value, caps);
      gst_caps_unref (caps);
    }
      break;
    case PROP_VIDEO_CAPTURE_CAPS:{
      GstCaps *caps = NULL;
      g_object_get (camera->videobin_capsfilter, "caps", &caps, NULL);
      gst_value_set_caps (value, caps);
      gst_caps_unref (caps);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_camera_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_camera_bin_debug, "camerabin2", 0, "CameraBin2");

  return gst_element_register (plugin, "camerabin2", GST_RANK_NONE,
      gst_camera_bin_get_type ());
}
