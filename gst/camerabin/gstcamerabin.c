/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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
 * SECTION:element-camerabin
 *
 * GstCameraBin is a high-level camera object that encapsulates the gstreamer
 * internals and provides a task based API for the application. It consists of
 * three main data paths: view-finder, image capture and video capture.
 *
 * <informalfigure>
 *   <mediaobject>
 *     <imageobject><imagedata fileref="camerabin.png"/></imageobject>
 *     <textobject><phrase>CameraBin structure</phrase></textobject>
 *     <caption><para>Structural decomposition of CameraBin object.</para></caption>
 *   </mediaobject>
 * </informalfigure>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m camerabin
 * ]|
 * </refsect2>
 * <refsect2>
 * <title>Image capture</title>
 * <para>
 * Image capture is selected by switching #GstCameraBin:mode to %MODE_IMAGE.
 * Taking still images is initiated with the #GstCameraBin::capture-start action
 * signal. Once the image has been captured, "image-captured" gst message is
 * posted to the bus and capturing another image is possible. If application 
 * has set #GstCameraBin:preview-caps property, then a "preview-image" gst
 * message is posted to bus containing preview image formatted according to
 * specified caps. Eventually when image has been saved
 * #GstCameraBin::image-done signal is emitted.
 * 
 * Available resolutions can be taken from the #GstCameraBin:video-source-caps
 * property. Image capture resolution can be set with 
 * #GstCameraBin::set-image-resolution action signal.
 *
 * Some video source elements implement the #GstPhotography interface, which contains
 * functions and properties for setting photography parameters. One can use
 * gst_bin_iterate_all_by_interface() to get a reference to it.
 *
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Video capture</title>
 * <para>
 * Video capture is selected by switching #GstCameraBin:mode to %MODE_VIDEO.
 * The capture is started with the #GstCameraBin::capture-start action signal
 * too. In addition to image capture one can use #GstCameraBin::capture-pause to
 * pause recording and #GstCameraBin::capture-stop to end recording.
 * 
 * Available resolutions and fps can be taken from the 
 * #GstCameraBin:video-source-caps property. 
 * #GstCameraBin::set-video-resolution-fps action signal can be used to set
 * frame rate and resolution for the video recording and view finder as well.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>States</title>
 * <para>
 * Elements within GstCameraBin are created and destroyed when switching
 * between NULL and READY states. Therefore element properties should be set
 * in NULL state. User set elements are not unreffed until GstCameraBin is
 * unreffed or replaced by a new user set element. Initially only elements
 * needed for view finder mode are created to speed up startup. Image bin and
 * video bin elements are created when setting the mode or starting capture.
 * GstCameraBin must be in the PLAYING state before #GstCameraBin::capture-start
 * is called.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Video and image previews</title>
 * <para>
 * GstCameraBin contains #GstCameraBin:preview-caps property, which is used to
 * determine whether the application wants a preview image of the captured
 * picture or video. When set, a GstMessage named "preview-image" will be sent.
 * This message will contain a GstBuffer holding the preview image, converted
 * to a format defined by those preview caps. The ownership of the preview
 * image is kept in GstCameraBin, so application should ref the preview buffer
 * object if it needs to use it elsewhere than in message handler.
 * 
 * Defining preview caps is done by selecting the capturing #GstCameraBin:mode
 * first and then setting the property. Camerabin remembers caps separately for
 * both modes, so it is not necessary to set the caps again after changing the
 * mode.
 * </para>
 * </refsect2>
 * <refsect2>
 * <note>
 * <para>
 * Since the muxers tested so far have problems with discontinous buffers, QoS
 * has been disabled, and then in order to record video, you MUST ensure that
 * there is enough CPU to encode the video. Thus choose smart resolution and
 * frames per second values. It is also highly recommended to avoid color
 * conversions; make sure all the elements involved work with the same
 * colorspace (i.e. rgb or yuv i420 or whatelse).
 * </para>
 * </note>
 * </refsect2>
 */

/*
 * The pipeline in the camerabin is
 *
 * videosrc [ ! ffmpegcsp ] ! capsfilter ! crop ! scale ! capsfilter ! \
 *     [ video_filter ! ] out-sel name=osel ! queue name=img_q
 *
 * View finder:
 * osel. ! in-sel name=isel ! scale ! capsfilter [ ! ffmpegcsp ] ! vfsink
 *
 * Image bin:
 * img_q. [ ! ipp ] ! ffmpegcsp ! imageenc ! metadatamux ! filesink
 *
 * Video bin:
 * osel. ! tee name=t ! queue ! videoenc ! videomux name=mux ! filesink
 * t. ! queue ! isel.
 * audiosrc ! queue ! audioconvert ! volume ! audioenc ! mux.
 *
 * The properties of elements are:
 *
 *   vfsink - "sync", FALSE, "qos", FALSE, "async", FALSE
 *   output-selector - "resend-latest", FALSE
 *   input-selector - "select-all", TRUE
 */

/*
 * includes
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>
/* FIXME: include #include <gst/gst-i18n-plugin.h> and use _(" ") */

#include "gstcamerabin.h"
#include "gstcamerabincolorbalance.h"

#include "camerabindebug.h"
#include "camerabingeneral.h"
#include "camerabinpreview.h"

#include "gstcamerabin-marshal.h"

/*
 * enum and types
 */

enum
{
  /* action signals */
  CAPTURE_START_SIGNAL,
  CAPTURE_STOP_SIGNAL,
  CAPTURE_PAUSE_SIGNAL,
  SET_VIDEO_RESOLUTION_FPS_SIGNAL,
  SET_IMAGE_RESOLUTION_SIGNAL,
  /* emit signals */
  IMG_DONE_SIGNAL,
  LAST_SIGNAL
};


/*
 * defines and static global vars
 */

static guint camerabin_signals[LAST_SIGNAL];

#define GST_TYPE_CAMERABIN_MODE (gst_camerabin_mode_get_type ())

/* default and range values for args */

#define DEFAULT_MODE MODE_IMAGE
#define DEFAULT_ZOOM 1.0
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_CAPTURE_WIDTH 800
#define DEFAULT_CAPTURE_HEIGHT 600
#define DEFAULT_FPS_N 0         /* makes it use the default */
#define DEFAULT_FPS_D 1

#define CAMERABIN_DEFAULT_VF_CAPS "video/x-raw-yuv,format=(fourcc)I420"
#define CAMERABIN_MAX_VF_WIDTH 848
#define CAMERABIN_MAX_VF_HEIGHT 848

#define DEFAULT_FLAGS GST_CAMERABIN_FLAG_SOURCE_RESIZE | \
  GST_CAMERABIN_FLAG_VIEWFINDER_SCALE | \
  GST_CAMERABIN_FLAG_VIEWFINDER_COLOR_CONVERSION | \
  GST_CAMERABIN_FLAG_IMAGE_COLOR_CONVERSION | \
  GST_CAMERABIN_FLAG_VIDEO_COLOR_CONVERSION | \
  GST_CAMERABIN_FLAG_AUDIO_CONVERSION

/* Using "bilinear" as default zoom method */
#define CAMERABIN_DEFAULT_ZOOM_METHOD 1

#define MIN_ZOOM 1.0
#define MAX_ZOOM 10.0
#define ZOOM_1X MIN_ZOOM

/* FIXME: this is v4l2camsrc specific */
#define DEFAULT_V4L2CAMSRC_DRIVER_NAME "omap3cam"

#define DEFAULT_BLOCK_VIEWFINDER FALSE
#define DEFAULT_READY_FOR_CAPTURE TRUE

/* message names */
#define PREVIEW_MESSAGE_NAME "preview-image"
#define IMG_CAPTURED_MESSAGE_NAME "image-captured"

#define CAMERABIN_PROCESSING_INC_UNLOCKED(c)  \
  (c)->processing_counter += 1;               \
  GST_DEBUG_OBJECT ((c), "Processing counter incremented to: %d", \
      (c)->processing_counter);               \
  if ((c)->processing_counter == 1)           \
    g_object_notify (G_OBJECT (c), "idle");

#define CAMERABIN_PROCESSING_DEC_UNLOCKED(c)  \
  (c)->processing_counter -= 1;               \
  GST_DEBUG_OBJECT ((c), "Processing counter decremented to: %d", \
      (c)->processing_counter);               \
  g_assert ((c)->processing_counter >= 0);    \
  if ((c)->processing_counter == 0) {         \
    g_cond_signal ((c)->idle_cond);           \
    g_object_notify (G_OBJECT (c), "idle");   \
  }

#define CAMERABIN_PROCESSING_INC(c)           \
  g_mutex_lock ((c)->capture_mutex);          \
  CAMERABIN_PROCESSING_INC_UNLOCKED ((c));    \
  g_mutex_unlock ((c)->capture_mutex);

#define CAMERABIN_PROCESSING_DEC(c)           \
  g_mutex_lock ((c)->capture_mutex);          \
  CAMERABIN_PROCESSING_DEC_UNLOCKED ((c));    \
  g_mutex_unlock ((c)->capture_mutex);

#define CAMERABIN_PROCESSING_WAIT_IDLE(c)             \
  g_mutex_lock ((c)->capture_mutex);                  \
  if ((c)->processing_counter > 0) {                  \
    GST_DEBUG_OBJECT ((c), "Waiting for processing operations to finish %d", \
        (c)->processing_counter);                     \
    g_cond_wait ((c)->idle_cond, (c)->capture_mutex); \
    GST_DEBUG_OBJECT ((c), "Processing operations finished"); \
  }                                                   \
  g_mutex_unlock ((c)->capture_mutex);

/*
 * static helper functions declaration
 */

static void camerabin_setup_src_elements (GstCameraBin * camera);

static gboolean camerabin_create_src_elements (GstCameraBin * camera);

static void camerabin_setup_view_elements (GstCameraBin * camera);

static gboolean camerabin_create_view_elements (GstCameraBin * camera);

static gboolean camerabin_create_elements (GstCameraBin * camera);

static void camerabin_destroy_elements (GstCameraBin * camera);

static void camerabin_dispose_elements (GstCameraBin * camera);

static void gst_camerabin_change_mode (GstCameraBin * camera, gint mode);

static void
gst_camerabin_set_flags (GstCameraBin * camera, GstCameraBinFlags flags);

static void
gst_camerabin_change_filename (GstCameraBin * camera, const gchar * name);

static void gst_camerabin_setup_zoom (GstCameraBin * camera);

static GstCaps *gst_camerabin_get_allowed_input_caps (GstCameraBin * camera);

static void gst_camerabin_rewrite_tags (GstCameraBin * camera);

static void
gst_camerabin_set_capsfilter_caps (GstCameraBin * camera, GstCaps * new_caps);

static void gst_camerabin_start_image_capture (GstCameraBin * camera);

static void gst_camerabin_start_video_recording (GstCameraBin * camera);

static void
camerabin_pad_blocked (GstPad * pad, gboolean blocked, gpointer user_data);

static gboolean
gst_camerabin_have_img_buffer (GstPad * pad, GstMiniObject * obj,
    gpointer u_data);
static gboolean
gst_camerabin_have_vid_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data);
static gboolean
gst_camerabin_have_queue_data (GstPad * pad, GstMiniObject * mini_obj,
    gpointer u_data);
static gboolean
gst_camerabin_have_src_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data);

static void gst_camerabin_reset_to_view_finder (GstCameraBin * camera);

static void gst_camerabin_do_stop (GstCameraBin * camera);

static void
gst_camerabin_set_allowed_framerate (GstCameraBin * camera,
    GstCaps * filter_caps);

static guint32 get_srcpad_current_format (GstElement * element);

static const GValue *gst_camerabin_find_better_framerate (GstCameraBin * camera,
    GstStructure * st, const GValue * orig_framerate);

static void
gst_camerabin_update_aspect_filter (GstCameraBin * camera, GstCaps * new_caps);

static void gst_camerabin_finish_image_capture (GstCameraBin * camera);
static void gst_camerabin_adapt_image_capture (GstCameraBin * camera,
    GstCaps * new_caps);
static void gst_camerabin_scene_mode_notify_cb (GObject * video_source,
    GParamSpec * pspec, gpointer user_data);
static void gst_camerabin_zoom_notify_cb (GObject * video_source,
    GParamSpec * pspec, gpointer user_data);
static void gst_camerabin_monitor_video_source_properties (GstCameraBin *
    camera);
static void gst_camerabin_configure_format (GstCameraBin * camera,
    GstCaps * caps);
static gboolean
copy_missing_fields (GQuark field_id, const GValue * value, gpointer user_data);

/*
 * GObject callback functions declaration
 */

static void gst_camerabin_dispose (GObject * object);

static void gst_camerabin_finalize (GObject * object);

static void gst_camerabin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_camerabin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/*
 * GstElement function declarations
 */

static GstStateChangeReturn
gst_camerabin_change_state (GstElement * element, GstStateChange transition);

static GstClock *gst_camerabin_provide_clock (GstElement * element);

/*
 * GstBin function declarations
 */
static void
gst_camerabin_handle_message_func (GstBin * bin, GstMessage * message);


/*
 * Action signal function declarations
 */

static void gst_camerabin_capture_start (GstCameraBin * camera);

static void gst_camerabin_capture_stop (GstCameraBin * camera);

static void gst_camerabin_capture_pause (GstCameraBin * camera);

static void
gst_camerabin_set_image_capture_caps (GstCameraBin * camera, gint width,
    gint height);

static void
gst_camerabin_set_video_resolution_fps (GstCameraBin * camera, gint width,
    gint height, gint fps_n, gint fps_d);
static void
do_set_video_resolution_fps (GstCameraBin * camera, gint width,
    gint height, gint fps_n, gint fps_d);

static void
gst_camerabin_set_image_resolution (GstCameraBin * camera, gint width,
    gint height);


/*
 * GST BOILERPLATE and GObject types
 */

static GType
gst_camerabin_mode_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {MODE_IMAGE, "Still image capture (default)", "mode-image"},
      {MODE_VIDEO, "Video recording", "mode-video"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstCameraBinMode", values);
  }
  return gtype;
}


static gboolean
gst_camerabin_iface_supported (GstImplementsInterface * iface, GType iface_type)
{
  GstCameraBin *camera = GST_CAMERABIN (iface);

  if (iface_type == GST_TYPE_COLOR_BALANCE) {
    if (camera->src_vid_src) {
      return GST_IS_COLOR_BALANCE (camera->src_vid_src);
    }
  } else if (iface_type == GST_TYPE_TAG_SETTER) {
    /* Note: Tag setter elements aren't
       present when image and video bin in NULL */
    GstElement *setter;
    setter = gst_bin_get_by_interface (GST_BIN (camera), iface_type);
    if (setter) {
      gst_object_unref (setter);
      return TRUE;
    } else {
      return FALSE;
    }
  }
  return FALSE;
}

static void
gst_camerabin_interface_init (GstImplementsInterfaceClass * klass)
{
  /*
   * default virtual functions
   */
  klass->supported = gst_camerabin_iface_supported;
}

static void
camerabin_init_interfaces (GType type)
{

  static const GInterfaceInfo camerabin_info = {
    (GInterfaceInitFunc) gst_camerabin_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo camerabin_color_balance_info = {
    (GInterfaceInitFunc) gst_camerabin_color_balance_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo camerabin_tagsetter_info = {
    NULL,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &camerabin_info);

  g_type_add_interface_static (type, GST_TYPE_COLOR_BALANCE,
      &camerabin_color_balance_info);

  g_type_add_interface_static (type, GST_TYPE_TAG_SETTER,
      &camerabin_tagsetter_info);
}

GST_BOILERPLATE_FULL (GstCameraBin, gst_camerabin, GstPipeline,
    GST_TYPE_PIPELINE, camerabin_init_interfaces);

/*
 * static helper functions implementation
 */

/*
 * camerabin_setup_src_elements:
 * @camera: camerabin object
 *
 * This function updates camerabin capsfilters according
 * to fps, resolution and zoom that have been configured
 * to camerabin.
 */
static void
camerabin_setup_src_elements (GstCameraBin * camera)
{
  GstStructure *st;
  GstCaps *new_caps;
  gboolean detect_framerate = FALSE;

  /* clear video update status */
  camera->video_capture_caps_update = FALSE;

  if (!camera->view_finder_caps) {
    st = gst_structure_from_string (CAMERABIN_DEFAULT_VF_CAPS, NULL);
  } else {
    st = gst_structure_copy (gst_caps_get_structure (camera->view_finder_caps,
            0));
  }

  gst_camerabin_monitor_video_source_properties (camera);

  if (camera->app_width > 0 && camera->app_height > 0) {
    gst_structure_set (st,
        "width", G_TYPE_INT, camera->app_width,
        "height", G_TYPE_INT, camera->app_height, NULL);
  }

  if (camera->app_fps_n > 0 && camera->app_fps_d > 0) {
    if (camera->night_mode) {
      GST_INFO_OBJECT (camera, "night mode, lowest allowed fps will be forced");
      camera->pre_night_fps_n = camera->app_fps_n;
      camera->pre_night_fps_d = camera->app_fps_d;
      detect_framerate = TRUE;
    } else {
      gst_structure_set (st,
          "framerate", GST_TYPE_FRACTION, camera->app_fps_n,
          camera->app_fps_d, NULL);
      new_caps = gst_caps_new_full (st, NULL);
    }
  } else {
    GST_DEBUG_OBJECT (camera, "no framerate specified");
    detect_framerate = TRUE;
  }

  if (detect_framerate) {
    GST_DEBUG_OBJECT (camera, "detecting allowed framerate");
    /* Remove old framerate if any */
    if (gst_structure_has_field (st, "framerate")) {
      gst_structure_remove_field (st, "framerate");
    }
    new_caps = gst_caps_new_full (st, NULL);

    /* Set allowed framerate for the resolution */
    gst_camerabin_set_allowed_framerate (camera, new_caps);
  }

  /* Set default zoom method */
  if (camera->src_zoom_scale) {
    g_object_set (camera->src_zoom_scale, "method",
        CAMERABIN_DEFAULT_ZOOM_METHOD, NULL);
  }
  /* we create new caps in any way and they take ownership of the structure st */
  gst_caps_replace (&camera->view_finder_caps, new_caps);
  gst_caps_unref (new_caps);

  /* Set caps for view finder mode */
  /* This also sets zoom */
  gst_camerabin_set_capsfilter_caps (camera, camera->view_finder_caps);
}

/*
 * camerabin_create_src_elements:
 * @camera: camerabin object
 *
 * This function creates and links upstream side elements for camerabin.
 * videosrc ! cspconv ! capsfilter ! crop ! scale ! capsfilter ! out-sel !
 *
 * Returns: TRUE, if elements were successfully created, FALSE otherwise
 */
static gboolean
camerabin_create_src_elements (GstCameraBin * camera)
{
  gboolean ret = FALSE;
  GstBin *cbin = GST_BIN (camera);
  gchar *driver_name = NULL;

  /* Add application set or default video src element */
  if (!(camera->src_vid_src = gst_camerabin_setup_default_element (cbin,
              camera->app_vid_src, "autovideosrc", DEFAULT_VIDEOSRC))) {
    camera->src_vid_src = NULL;
    goto done;
  } else {
    if (!gst_camerabin_add_element (cbin, camera->src_vid_src))
      goto done;
  }
  if (camera->flags & GST_CAMERABIN_FLAG_SOURCE_COLOR_CONVERSION) {
    if (!gst_camerabin_create_and_add_element (cbin, "ffmpegcolorspace",
            "src-ffmpegcolorspace"))
      goto done;
  }
  if (!(camera->src_filter =
          gst_camerabin_create_and_add_element (cbin, "capsfilter",
              "src-capsfilter")))
    goto done;
  if (camera->flags & GST_CAMERABIN_FLAG_SOURCE_RESIZE) {
    if (!(camera->src_zoom_crop =
            gst_camerabin_create_and_add_element (cbin, "videocrop",
                "src-videocrop")))
      goto done;
    if (!(camera->src_zoom_scale =
            gst_camerabin_create_and_add_element (cbin, "videoscale",
                "src-videoscale")))
      goto done;
    if (!(camera->src_zoom_filter =
            gst_camerabin_create_and_add_element (cbin, "capsfilter",
                "src-resize-capsfilter")))
      goto done;
  }
  if (camera->app_video_filter) {
    if (!gst_camerabin_add_element (cbin, camera->app_video_filter)) {
      goto done;
    }
  }
  if (!(camera->src_out_sel =
          gst_camerabin_create_and_add_element (cbin, "output-selector", NULL)))
    goto done;

  /* Set pad-negotiation-mode to active */
  g_object_set (camera->src_out_sel, "pad-negotiation-mode", 2, NULL);

  /* Set default "driver-name" for v4l2camsrc if not set */
  /* FIXME: v4l2camsrc specific */
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (camera->src_vid_src),
          "driver-name")) {
    g_object_get (G_OBJECT (camera->src_vid_src), "driver-name",
        &driver_name, NULL);
    if (!driver_name) {
      g_object_set (G_OBJECT (camera->src_vid_src), "driver-name",
          DEFAULT_V4L2CAMSRC_DRIVER_NAME, NULL);
    }
  }

  ret = TRUE;
done:
  return ret;
}

/*
 * camerabin_setup_view_elements:
 * @camera: camerabin object
 *
 * This function configures properties for view finder sink element.
 */
static void
camerabin_setup_view_elements (GstCameraBin * camera)
{
  GST_DEBUG_OBJECT (camera, "setting view finder properties");
  g_object_set (G_OBJECT (camera->view_in_sel), "select-all", TRUE, NULL);
  /* Set properties for view finder sink */
  /* Find the actual sink if using bin like autovideosink */
  if (GST_IS_BIN (camera->view_sink)) {
    GList *child = NULL, *children = GST_BIN_CHILDREN (camera->view_sink);
    for (child = children; child != NULL; child = g_list_next (child)) {
      GObject *ch = G_OBJECT (child->data);
      if (g_object_class_find_property (G_OBJECT_GET_CLASS (ch), "sync")) {
        g_object_set (G_OBJECT (ch), "sync", FALSE, "qos", FALSE, "async",
            FALSE, NULL);
      }
    }
  } else {
    g_object_set (G_OBJECT (camera->view_sink), "sync", FALSE, "qos", FALSE,
        "async", FALSE, NULL);
  }
}

/*
 * camerabin_create_view_elements:
 * @camera: camerabin object
 *
 * This function creates and links downstream side elements for camerabin.
 * ! scale ! cspconv ! view finder sink
 *
 * Returns: TRUE, if elements were successfully created, FALSE otherwise
 */
static gboolean
camerabin_create_view_elements (GstCameraBin * camera)
{
  const GList *pads;
  GstBin *cbin = GST_BIN (camera);

  if (!(camera->view_in_sel =
          gst_camerabin_create_and_add_element (cbin, "input-selector",
              NULL))) {
    goto error;
  }

  /* Look for recently added input selector sink pad, we need to release it later */
  pads = GST_ELEMENT_PADS (camera->view_in_sel);
  while (pads != NULL
      && (GST_PAD_DIRECTION (GST_PAD (pads->data)) != GST_PAD_SINK)) {
    pads = g_list_next (pads);
  }
  camera->pad_view_src = GST_PAD (pads->data);

  /* Add videoscale in case we need to downscale frame for view finder */
  if (camera->flags & GST_CAMERABIN_FLAG_VIEWFINDER_SCALE) {
    if (!(camera->view_scale =
            gst_camerabin_create_and_add_element (cbin, "videoscale",
                "vf-videoscale"))) {
      goto error;
    }

    /* Add capsfilter to maintain aspect ratio while scaling */
    if (!(camera->aspect_filter =
            gst_camerabin_create_and_add_element (cbin, "capsfilter",
                "vf-scale-capsfilter"))) {
      goto error;
    }
  }
  if (camera->flags & GST_CAMERABIN_FLAG_VIEWFINDER_COLOR_CONVERSION) {
    if (!gst_camerabin_create_and_add_element (cbin, "ffmpegcolorspace",
            "vf-ffmpegcolorspace")) {
      goto error;
    }
  }

  if (camera->app_viewfinder_filter) {
    if (!gst_camerabin_add_element (GST_BIN (camera),
            camera->app_viewfinder_filter)) {
      goto error;
    }
  }

  /* Add application set or default video sink element */
  if (!(camera->view_sink = gst_camerabin_setup_default_element (cbin,
              camera->app_vf_sink, "autovideosink", DEFAULT_VIDEOSINK))) {
    camera->view_sink = NULL;
    goto error;
  } else {
    if (!gst_camerabin_add_element (cbin, camera->view_sink))
      goto error;
  }

  return TRUE;
error:
  return FALSE;
}

/*
 * camerabin_create_elements:
 * @camera: camerabin object
 *
 * This function creates and links all elements for camerabin,
 *
 * Returns: TRUE, if elements were successfully created, FALSE otherwise
 */
static gboolean
camerabin_create_elements (GstCameraBin * camera)
{
  gboolean ret = FALSE;
  GstPadLinkReturn link_ret = GST_PAD_LINK_REFUSED;
  GstPad *unconnected_pad;

  GST_LOG_OBJECT (camera, "creating elements");

  /* Create "src" elements */
  if (!camerabin_create_src_elements (camera)) {
    goto done;
  }

  camera->pad_src_img =
      gst_element_get_request_pad (camera->src_out_sel, "src%d");

  gst_pad_add_data_probe (camera->pad_src_img,
      G_CALLBACK (gst_camerabin_have_img_buffer), camera);

  /* Add queue leading to image bin */
  camera->img_queue = gst_element_factory_make ("queue", "image-queue");
  if (!gst_camerabin_add_element (GST_BIN (camera), camera->img_queue)) {
    goto done;
  }

  /* To avoid deadlock, we won't restrict the image queue size */
  /* FIXME: actually we would like to have some kind of restriction here (size),
     but deadlocks must be handled somehow... */
  g_object_set (G_OBJECT (camera->img_queue), "max-size-buffers", 0,
      "max-size-bytes", 0, "max-size-time", G_GUINT64_CONSTANT (0), NULL);
  g_object_set (camera->img_queue, "silent", TRUE, NULL);

  camera->pad_src_queue = gst_element_get_static_pad (camera->img_queue, "src");

  gst_pad_add_data_probe (camera->pad_src_queue,
      G_CALLBACK (gst_camerabin_have_queue_data), camera);

  /* Add image bin */
  if (!gst_camerabin_add_element (GST_BIN (camera), camera->imgbin)) {
    goto done;
  }

  camera->pad_src_view =
      gst_element_get_request_pad (camera->src_out_sel, "src%d");

  /* Create view finder elements */
  if (!camerabin_create_view_elements (camera)) {
    GST_WARNING_OBJECT (camera, "creating view finder elements failed");
    goto done;
  }

  /* Set view finder active as default */
  g_object_set (G_OBJECT (camera->src_out_sel), "active-pad",
      camera->pad_src_view, NULL);

  /* Add video bin */
  camera->pad_src_vid =
      gst_element_get_request_pad (camera->src_out_sel, "src%d");
  if (!gst_camerabin_add_element (GST_BIN (camera), camera->vidbin)) {
    goto done;
  }
  gst_pad_add_buffer_probe (camera->pad_src_vid,
      G_CALLBACK (gst_camerabin_have_vid_buffer), camera);

  /* Link video bin ! view finder */
  unconnected_pad = gst_bin_find_unlinked_pad (GST_BIN (camera), GST_PAD_SRC);
  camera->pad_view_vid =
      gst_element_get_request_pad (camera->view_in_sel, "sink%d");
  link_ret =
      gst_pad_link_full (unconnected_pad, camera->pad_view_vid,
      GST_PAD_LINK_CHECK_CAPS);
  gst_object_unref (unconnected_pad);
  if (GST_PAD_LINK_FAILED (link_ret)) {
    GST_ELEMENT_ERROR (camera, CORE, NEGOTIATION, (NULL),
        ("linking video bin and view finder failed"));
    goto done;
  }

  ret = TRUE;

done:

  if (FALSE == ret)
    camerabin_destroy_elements (camera);

  return ret;
}

/*
 * camerabin_destroy_elements:
 * @camera: camerabin object
 *
 * This function removes all elements from camerabin.
 */
static void
camerabin_destroy_elements (GstCameraBin * camera)
{
  GST_DEBUG_OBJECT (camera, "destroying elements");

  /* Release request pads */
  if (camera->pad_view_vid) {
    gst_element_release_request_pad (camera->view_in_sel, camera->pad_view_vid);
    gst_object_unref (camera->pad_view_vid);
    camera->pad_view_vid = NULL;
  }
  if (camera->pad_src_vid) {
    gst_element_release_request_pad (camera->src_out_sel, camera->pad_src_vid);
    gst_object_unref (camera->pad_src_vid);
    camera->pad_src_vid = NULL;
  }
  if (camera->pad_src_img) {
    gst_element_release_request_pad (camera->src_out_sel, camera->pad_src_img);
    gst_object_unref (camera->pad_src_img);
    camera->pad_src_img = NULL;
  }
  if (camera->pad_view_src) {
    gst_element_release_request_pad (camera->view_in_sel, camera->pad_view_src);
    /* don't unref, we have not requested it */
    camera->pad_view_src = NULL;
  }
  if (camera->pad_src_view) {
    gst_element_release_request_pad (camera->src_out_sel, camera->pad_src_view);
    gst_object_unref (camera->pad_src_view);
    camera->pad_src_view = NULL;
  }

  if (camera->pad_src_queue) {
    gst_object_unref (camera->pad_src_queue);
    camera->pad_src_queue = NULL;
  }

  /* view finder elements */
  camera->view_in_sel = NULL;
  camera->view_scale = NULL;
  camera->aspect_filter = NULL;
  camera->view_sink = NULL;

  /* source elements */
  camera->src_vid_src = NULL;
  camera->src_filter = NULL;
  camera->src_zoom_crop = NULL;
  camera->src_zoom_scale = NULL;
  camera->src_zoom_filter = NULL;
  camera->src_out_sel = NULL;

  camera->active_bin = NULL;

  /* Reset caps data as the elements might be completely different next
   * time we 'start' */
  if (camera->view_finder_caps) {
    gst_caps_replace (&camera->view_finder_caps, NULL);
  }
  gst_caps_replace (&camera->allowed_caps, NULL);
  camera->fps_n = camera->fps_d = 0;
  camera->width = camera->height = 0;

  /* Remove elements */
  gst_camerabin_remove_elements_from_bin (GST_BIN (camera));
}

/*
 * camerabin_dispose_elements:
 * @camera: camerabin object
 *
 * This function releases all allocated camerabin resources.
 */
static void
camerabin_dispose_elements (GstCameraBin * camera)
{
  GST_INFO ("cleaning");

  if (camera->capture_mutex) {
    g_mutex_free (camera->capture_mutex);
    camera->capture_mutex = NULL;
  }
  if (camera->cond) {
    g_cond_free (camera->cond);
    camera->cond = NULL;
  }
  if (camera->idle_cond) {
    g_cond_free (camera->idle_cond);
    camera->idle_cond = NULL;
  }
  if (camera->filename) {
    g_string_free (camera->filename, TRUE);
    camera->filename = NULL;
  }
  /* Unref application set elements */
  if (camera->app_vf_sink) {
    gst_object_unref (camera->app_vf_sink);
    camera->app_vf_sink = NULL;
  }
  if (camera->app_vid_src) {
    gst_object_unref (camera->app_vid_src);
    camera->app_vid_src = NULL;
  }

  if (camera->app_video_filter) {
    gst_object_unref (camera->app_video_filter);
    camera->app_video_filter = NULL;
  }

  if (camera->app_viewfinder_filter) {
    gst_object_unref (camera->app_viewfinder_filter);
    camera->app_viewfinder_filter = NULL;
  }

  if (camera->app_preview_source_filter) {
    gst_object_unref (camera->app_preview_source_filter);
    camera->app_preview_source_filter = NULL;
  }

  if (camera->app_video_preview_source_filter) {
    gst_object_unref (camera->app_video_preview_source_filter);
    camera->app_video_preview_source_filter = NULL;
  }

  /* Free caps */
  gst_caps_replace (&camera->image_capture_caps, NULL);
  gst_caps_replace (&camera->view_finder_caps, NULL);
  gst_caps_replace (&camera->allowed_caps, NULL);
  gst_caps_replace (&camera->preview_caps, NULL);
  gst_caps_replace (&camera->video_preview_caps, NULL);
  gst_buffer_replace (&camera->video_preview_buffer, NULL);

  if (camera->event_tags) {
    gst_tag_list_free (camera->event_tags);
    camera->event_tags = NULL;
  }
}

/*
 * gst_camerabin_image_capture_continue:
 * @camera: camerabin object
 * @filename: filename of the finished image
 *
 * Notify application that image has been saved with a signal.
 *
 * Returns TRUE if another image should be captured, FALSE otherwise.
 */
static gboolean
gst_camerabin_image_capture_continue (GstCameraBin * camera,
    const gchar * filename)
{
  gboolean cont = FALSE;

  GST_DEBUG_OBJECT (camera, "emitting img_done signal, filename: %s", filename);
  g_signal_emit (G_OBJECT (camera), camerabin_signals[IMG_DONE_SIGNAL], 0,
      filename, &cont);

  /* If the app wants to continue make sure new filename has been set */
  if (cont && g_str_equal (camera->filename->str, "")) {
    GST_ELEMENT_ERROR (camera, RESOURCE, NOT_FOUND,
        ("cannot continue capture, no filename has been set"), (NULL));
    cont = FALSE;
  }

  return cont;
}

/*
 * gst_camerabin_change_mode:
 * @camera: camerabin object
 * @mode: image or video mode
 *
 * Change camerabin mode between image and video capture.
 * Changing mode will stop ongoing capture.
 */
static void
gst_camerabin_change_mode (GstCameraBin * camera, gint mode)
{
  if (camera->mode != mode || !camera->active_bin) {
    GstState state, pending_state;

    GST_DEBUG_OBJECT (camera, "setting mode: %d (old_mode=%d)",
        mode, camera->mode);
    /* Interrupt ongoing capture */
    gst_camerabin_do_stop (camera);

    /* reset night-mode stored values */
    camera->pre_night_fps_n = 0;
    camera->pre_night_fps_d = 1;

    camera->mode = mode;
    gst_element_get_state (GST_ELEMENT (camera), &state, &pending_state, 0);
    if (state == GST_STATE_PAUSED || state == GST_STATE_PLAYING ||
        pending_state == GST_STATE_PAUSED
        || pending_state == GST_STATE_PLAYING) {
      if (camera->active_bin) {
        GST_DEBUG_OBJECT (camera, "stopping active bin");
        gst_element_set_state (camera->active_bin, GST_STATE_READY);
      }
      if (camera->mode == MODE_IMAGE) {
        GstStateChangeReturn state_ret;

        camera->active_bin = camera->imgbin;
        state_ret =
            gst_element_set_state (camera->active_bin, GST_STATE_PAUSED);

        if (state_ret == GST_STATE_CHANGE_FAILURE) {
          GST_WARNING_OBJECT (camera, "state change failed");
          gst_element_set_state (camera->active_bin, GST_STATE_NULL);
          camera->active_bin = NULL;
        }
      } else if (camera->mode == MODE_VIDEO) {
        camera->active_bin = camera->vidbin;
      }
      gst_camerabin_reset_to_view_finder (camera);
    } else if (camera->mode == MODE_IMAGE) {
      /* Prepare needed elements for image processing */
      gst_camerabin_image_prepare_elements (GST_CAMERABIN_IMAGE
          (camera->imgbin));
    }
  }
}

/*
 * gst_camerabin_set_flags:
 * @camera: camerabin object
 * @flags: flags for camerabin, videobin and imagebin
 *
 * Change camerabin capture flags.
 */
static void
gst_camerabin_set_flags (GstCameraBin * camera, GstCameraBinFlags flags)
{
  g_return_if_fail (camera != NULL);

  GST_DEBUG_OBJECT (camera, "setting flags: %d", flags);

  GST_OBJECT_LOCK (camera);
  camera->flags = flags;
  GST_OBJECT_UNLOCK (camera);

  gst_camerabin_video_set_flags (GST_CAMERABIN_VIDEO (camera->vidbin), flags);
  gst_camerabin_image_set_flags (GST_CAMERABIN_IMAGE (camera->imgbin), flags);
}

/*
 * gst_camerabin_change_filename:
 * @camera: camerabin object
 * @name: new filename for capture
 *
 * Change filename for image or video capture.
 */
static void
gst_camerabin_change_filename (GstCameraBin * camera, const gchar * name)
{
  if (name == NULL)
    name = "";

  if (0 != strcmp (camera->filename->str, name)) {
    GST_DEBUG_OBJECT (camera, "changing filename from '%s' to '%s'",
        camera->filename->str, name);
    g_string_assign (camera->filename, name);
  }
}

static gboolean
gst_camerabin_set_videosrc_zoom (GstCameraBin * camera, gfloat zoom)
{
  gboolean ret = FALSE;

  /* Try with photography interface zooming */
  if (GST_IS_ELEMENT (camera->src_vid_src) &&
      gst_element_implements_interface (camera->src_vid_src,
          GST_TYPE_PHOTOGRAPHY)) {
    gst_photography_set_zoom (GST_PHOTOGRAPHY (camera->src_vid_src), zoom);
    ret = TRUE;
  }
  return ret;
}


static gboolean
gst_camerabin_set_element_zoom (GstCameraBin * camera, gfloat zoom)
{
  gint w2_crop = 0, h2_crop = 0;
  GstPad *pad_zoom_sink = NULL;
  gboolean ret = FALSE;
  gint left = camera->base_crop_left;
  gint right = camera->base_crop_right;
  gint top = camera->base_crop_top;
  gint bottom = camera->base_crop_bottom;

  if (camera->src_zoom_crop) {
    /* Update capsfilters to apply the zoom */
    GST_INFO_OBJECT (camera, "zoom: %f, orig size: %dx%d", zoom,
        camera->width, camera->height);

    if (zoom != ZOOM_1X) {
      w2_crop = (camera->width - (camera->width * ZOOM_1X / zoom)) / 2;
      h2_crop = (camera->height - (camera->height * ZOOM_1X / zoom)) / 2;

      left += w2_crop;
      right += w2_crop;
      top += h2_crop;
      bottom += h2_crop;

      /* force number of pixels cropped from left to be even, to avoid slow code
       * path on videoscale */
      left &= 0xFFFE;
    }

    pad_zoom_sink = gst_element_get_static_pad (camera->src_zoom_crop, "sink");

    GST_INFO_OBJECT (camera,
        "sw cropping: left:%d, right:%d, top:%d, bottom:%d", left, right, top,
        bottom);

    GST_PAD_STREAM_LOCK (pad_zoom_sink);
    g_object_set (camera->src_zoom_crop, "left", left, "right", right, "top",
        top, "bottom", bottom, NULL);
    GST_PAD_STREAM_UNLOCK (pad_zoom_sink);
    gst_object_unref (pad_zoom_sink);
    ret = TRUE;
  }
  return ret;
}

/*
 * gst_camerabin_setup_zoom:
 * @camera: camerabin object
 *
 * Apply zoom configured to camerabin to capture.
 */
static void
gst_camerabin_setup_zoom (GstCameraBin * camera)
{
  gfloat zoom;

  g_return_if_fail (camera != NULL);

  zoom = camera->zoom;

  g_return_if_fail (zoom);

  GST_INFO_OBJECT (camera, "setting zoom %f", zoom);

  if (gst_camerabin_set_videosrc_zoom (camera, zoom)) {
    gst_camerabin_set_element_zoom (camera, ZOOM_1X);
    GST_INFO_OBJECT (camera, "zoom set using videosrc");
  } else if (gst_camerabin_set_element_zoom (camera, zoom)) {
    GST_INFO_OBJECT (camera, "zoom set using gst elements");
  } else {
    GST_INFO_OBJECT (camera, "setting zoom failed");
  }
}

/*
 * gst_camerabin_get_allowed_input_caps:
 * @camera: camerabin object
 *
 * Retrieve caps from videosrc describing formats it supports
 *
 * Returns: caps object from videosrc
 */
static GstCaps *
gst_camerabin_get_allowed_input_caps (GstCameraBin * camera)
{
  GstCaps *caps = NULL;
  GstPad *pad = NULL, *peer_pad = NULL;
  GstState state;
  GstElement *videosrc;

  g_return_val_if_fail (camera != NULL, NULL);

  videosrc = camera->src_vid_src ? camera->src_vid_src : camera->app_vid_src;

  if (!videosrc) {
    GST_WARNING_OBJECT (camera, "no videosrc, can't get allowed caps");
    goto failed;
  }

  if (camera->allowed_caps) {
    GST_DEBUG_OBJECT (camera, "returning cached caps");
    goto done;
  }

  pad = gst_element_get_static_pad (videosrc, "src");

  if (!pad) {
    GST_WARNING_OBJECT (camera, "no srcpad in videosrc");
    goto failed;
  }

  state = GST_STATE (videosrc);

  /* Make this function work also in NULL state */
  if (state == GST_STATE_NULL) {
    GST_DEBUG_OBJECT (camera, "setting videosrc to ready temporarily");
    peer_pad = gst_pad_get_peer (pad);
    if (peer_pad) {
      gst_pad_unlink (pad, peer_pad);
    }
    /* Set videosrc to READY to open video device */
    gst_element_set_locked_state (videosrc, TRUE);
    gst_element_set_state (videosrc, GST_STATE_READY);
  }

  camera->allowed_caps = gst_pad_get_caps (pad);

  /* Restore state and re-link if necessary */
  if (state == GST_STATE_NULL) {
    GST_DEBUG_OBJECT (camera, "restoring videosrc state %d", state);
    /* Reset videosrc to NULL state, some drivers seem to need this */
    gst_element_set_state (videosrc, GST_STATE_NULL);
    if (peer_pad) {
      gst_pad_link_full (pad, peer_pad, GST_PAD_LINK_CHECK_CAPS);
      gst_object_unref (peer_pad);
    }
    gst_element_set_locked_state (videosrc, FALSE);
  }

  gst_object_unref (pad);

done:
  if (camera->allowed_caps) {
    caps = gst_caps_copy (camera->allowed_caps);
  }
  GST_DEBUG_OBJECT (camera, "allowed caps:%" GST_PTR_FORMAT, caps);
failed:
  return caps;
}

/*
 * gst_camerabin_send_img_queue_event:
 * @camera: camerabin object
 * @event: event to be sent
 *
 * Send the given event to image queue.
 */
static void
gst_camerabin_send_img_queue_event (GstCameraBin * camera, GstEvent * event)
{
  GstPad *queue_sink;

  g_return_if_fail (camera != NULL);
  g_return_if_fail (event != NULL);

  queue_sink = gst_element_get_static_pad (camera->img_queue, "sink");
  gst_pad_send_event (queue_sink, event);
  gst_object_unref (queue_sink);
}

/*
 * gst_camerabin_send_img_queue_custom_event:
 * @camera: camerabin object
 * @ev_struct: event structure to be sent
 *
 * Generate and send a custom event to image queue.
 */
static void
gst_camerabin_send_img_queue_custom_event (GstCameraBin * camera,
    GstStructure * ev_struct)
{
  GstEvent *event;

  g_return_if_fail (camera != NULL);
  g_return_if_fail (ev_struct != NULL);

  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, ev_struct);
  gst_camerabin_send_img_queue_event (camera, event);
}

/*
 * gst_camerabin_rewrite_tags_to_bin:
 * @bin: bin holding tag setter elements
 * @list: tag list to be written
 *
 * This function looks for certain tag setters from given bin
 * and REPLACES ALL setter tags with given tag list
 *
 */
static void
gst_camerabin_rewrite_tags_to_bin (GstBin * bin, const GstTagList * list)
{
  GstElement *setter;
  GstIterator *iter;
  GstIteratorResult res = GST_ITERATOR_OK;
  gpointer data;

  iter = gst_bin_iterate_all_by_interface (bin, GST_TYPE_TAG_SETTER);

  while (res == GST_ITERATOR_OK || res == GST_ITERATOR_RESYNC) {
    res = gst_iterator_next (iter, &data);
    switch (res) {
      case GST_ITERATOR_DONE:
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING ("error iterating tag setters");
        break;
      case GST_ITERATOR_OK:
        setter = GST_ELEMENT (data);
        GST_LOG ("iterating tag setters: %" GST_PTR_FORMAT, setter);
        GST_DEBUG ("replacement tags %" GST_PTR_FORMAT, list);
        gst_tag_setter_merge_tags (GST_TAG_SETTER (setter), list,
            GST_TAG_MERGE_REPLACE_ALL);
        gst_object_unref (setter);
        break;
      default:
        break;
    }
  }

  gst_iterator_free (iter);
}

/*
 * gst_camerabin_get_internal_tags:
 * @camera: the camera bin element
 *
 * Returns tag list containing metadata from camerabin
 * and it's elements
 */
static GstTagList *
gst_camerabin_get_internal_tags (GstCameraBin * camera)
{
  GstTagList *list = gst_tag_list_new ();
  GstColorBalance *balance = NULL;
  const GList *controls = NULL, *item;
  GstColorBalanceChannel *channel;
  gint min_value, max_value, mid_value, cur_value;

  if (camera->active_bin == camera->vidbin) {
    /* FIXME: check if internal video tag setting is needed */
    goto done;
  }

  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_CAPTURING_DIGITAL_ZOOM_RATIO, (gdouble) camera->zoom, NULL);

  if (gst_element_implements_interface (GST_ELEMENT (camera),
          GST_TYPE_COLOR_BALANCE)) {
    balance = GST_COLOR_BALANCE (camera);
  }

  if (balance) {
    controls = gst_color_balance_list_channels (balance);
  }
  for (item = controls; item; item = g_list_next (item)) {
    channel = item->data;
    min_value = channel->min_value;
    max_value = channel->max_value;
    /* the default value would probably better */
    mid_value = min_value + ((max_value - min_value) / 2);
    cur_value = gst_color_balance_get_value (balance, channel);

    if (!g_ascii_strcasecmp (channel->label, "brightness")) {
      /* The value of brightness. The unit is the APEX value (Additive System of Photographic Exposure).
       * Ordinarily it is given in the range of -99.99 to 99.99. Note that
       * if the numerator of the recorded value is 0xFFFFFFFF, Unknown shall be indicated.
       *
       * BrightnessValue (Bv) = log2 ( B/NK )
       * Note that: B:cd/cm² (candela per square centimeter), N,K: constant
       *
       * http://johnlind.tripod.com/science/scienceexposure.html
       *
       */
/*
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          "capture-brightness", cur_value, 1, NULL);
*/
    } else if (!g_ascii_strcasecmp (channel->label, "contrast")) {
      /* 0 = Normal, 1 = Soft, 2 = Hard */

      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_CAPTURING_CONTRAST,
          (cur_value == mid_value) ? "normal" : ((cur_value < mid_value)
              ? "soft" : "hard"), NULL);
    } else if (!g_ascii_strcasecmp (channel->label, "gain")) {
      /* 0 = Normal, 1 = Low Up, 2 = High Up, 3 = Low Down, 4 = Hight Down */
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_CAPTURING_GAIN_ADJUSTMENT,
          (cur_value == mid_value) ? "normal" : ((cur_value <
                  mid_value) ? "low-gain-up" : "low-gain-down"), NULL);
    } else if (!g_ascii_strcasecmp (channel->label, "saturation")) {
      /* 0 = Normal, 1 = Low, 2 = High */
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_CAPTURING_SATURATION,
          (cur_value == mid_value) ? "normal" : ((cur_value < mid_value)
              ? "low-saturation" : "high-saturation"), NULL);
    }
  }

done:

  return list;
}

/*
 * gst_camerabin_rewrite_tags:
 * @camera: the camera bin element
 *
 * Merges application set tags to camerabin internal tags,
 * and writes them using image or video bin tag setters.
 */
static void
gst_camerabin_rewrite_tags (GstCameraBin * camera)
{
  const GstTagList *app_tag_list = NULL;
  GstTagList *list = NULL;

  /* Get application set tags */
  app_tag_list = gst_tag_setter_get_tag_list (GST_TAG_SETTER (camera));

  /* Get tags from camerabin and it's elements */
  list = gst_camerabin_get_internal_tags (camera);

  if (app_tag_list) {
    gst_tag_list_insert (list, app_tag_list, GST_TAG_MERGE_REPLACE);
  }

  /* Write tags */
  if (camera->active_bin == camera->vidbin) {
    gst_camerabin_rewrite_tags_to_bin (GST_BIN (camera->active_bin), list);
  } else {
    /* Image tags need to be sent as a serialized event into image queue */
    GstEvent *tagevent = gst_event_new_tag (gst_tag_list_copy (list));
    gst_camerabin_send_img_queue_event (camera, tagevent);
  }

  gst_tag_list_free (list);
}

/*
 * gst_camerabin_set_capsfilter_caps:
 * @camera: camerabin object
 * @new_caps: pointer to caps object to set
 *
 * Set given caps to camerabin capsfilters.
 */
static void
gst_camerabin_set_capsfilter_caps (GstCameraBin * camera, GstCaps * new_caps)
{
  GST_INFO_OBJECT (camera, "new_caps:%" GST_PTR_FORMAT, new_caps);

  gst_camerabin_configure_format (camera, new_caps);

  /* Update zoom */
  gst_camerabin_setup_zoom (camera);

  /* Update capsfilters */
  g_object_set (G_OBJECT (camera->src_filter), "caps", new_caps, NULL);
  if (camera->src_zoom_filter)
    g_object_set (G_OBJECT (camera->src_zoom_filter), "caps", new_caps, NULL);
  gst_camerabin_update_aspect_filter (camera, new_caps);
  GST_INFO_OBJECT (camera, "udpated");
}

/*
 * img_capture_prepared:
 * @data: camerabin object
 * @caps: caps describing the prepared image format
 *
 * Callback which is called after image capture has been prepared.
 */
static void
img_capture_prepared (gpointer data, GstCaps * caps)
{
  GstCameraBin *camera = GST_CAMERABIN (data);

  GST_INFO_OBJECT (camera, "image capture prepared");

  /* It is possible we are about to get something else that we requested */
  if (!gst_caps_is_equal (camera->image_capture_caps, caps)) {
    gst_camerabin_adapt_image_capture (camera, caps);
  } else {
    gst_camerabin_set_capsfilter_caps (camera, camera->image_capture_caps);
  }

  g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
      "active-pad", camera->pad_src_img, NULL);
}

/*
 * gst_camerabin_start_image_capture:
 * @camera: camerabin object
 *
 * Initiates image capture.
 */
static void
gst_camerabin_start_image_capture (GstCameraBin * camera)
{
  gboolean wait_for_prepare = FALSE, ret = FALSE;

  GST_INFO_OBJECT (camera, "starting image capture");

  if (GST_IS_ELEMENT (camera->src_vid_src) &&
      gst_element_implements_interface (camera->src_vid_src,
          GST_TYPE_PHOTOGRAPHY)) {
    /* Start image capture preparations using photography iface */
    wait_for_prepare = TRUE;
    g_mutex_lock (camera->capture_mutex);

    /* Enable still image capture mode in v4l2camsrc */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (camera->src_vid_src),
            "capture-mode")) {
      g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 1, NULL);
    }

    if (!camera->image_capture_caps || camera->image_capture_caps_update) {
      if (camera->image_capture_width && camera->image_capture_height) {
        /* Resolution is set, but it isn't in use yet */
        gst_camerabin_set_image_capture_caps (camera,
            camera->image_capture_width, camera->image_capture_height);
      } else {
        /* Capture resolution not set. Use viewfinder resolution */
        camera->image_capture_caps = gst_caps_copy (camera->view_finder_caps);
        camera->image_capture_caps_update = FALSE;
      }
    }

    /* Start preparations for image capture */
    GST_DEBUG_OBJECT (camera, "prepare image capture caps %" GST_PTR_FORMAT,
        camera->image_capture_caps);
    ret =
        gst_photography_prepare_for_capture (GST_PHOTOGRAPHY
        (camera->src_vid_src), (GstPhotoCapturePrepared) img_capture_prepared,
        camera->image_capture_caps, camera);
    camera->capturing = TRUE;
    g_mutex_unlock (camera->capture_mutex);
  }

  if (!wait_for_prepare) {
    g_mutex_lock (camera->capture_mutex);
    g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", TRUE,
        "active-pad", camera->pad_src_img, NULL);
    camera->capturing = TRUE;
    ret = TRUE;
    g_mutex_unlock (camera->capture_mutex);
  }

  if (!ret) {
    CAMERABIN_PROCESSING_DEC_UNLOCKED (camera);
    GST_WARNING_OBJECT (camera, "starting image capture failed");
  }
}

 /*
  * FIXME ideally a caps renegotiation is better here
  */
static void
reset_video_capture_caps (GstCameraBin * camera)
{
  GstState state, pending;
  GstPad *activepad = NULL;

  GST_INFO_OBJECT (camera, "switching resolution to %dx%d and fps to %d/%d",
      camera->width, camera->height, camera->fps_n, camera->fps_d);

  /* Interrupt ongoing capture */
  gst_camerabin_do_stop (camera);

  /* prevent image captures from being lost */
  CAMERABIN_PROCESSING_WAIT_IDLE (camera);

  gst_element_get_state (GST_ELEMENT (camera), &state, &pending, 0);
  if (state == GST_STATE_PAUSED || state == GST_STATE_PLAYING) {
    GST_INFO_OBJECT (camera,
        "changing to READY to initialize videosrc with new format");
    g_object_get (G_OBJECT (camera->src_out_sel), "active-pad", &activepad,
        NULL);
    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_READY);
  }
  if (pending != GST_STATE_VOID_PENDING) {
    GST_LOG_OBJECT (camera, "restoring pending state: %s",
        gst_element_state_get_name (pending));
    state = pending;
  }

  /* Re-set the active pad since switching camerabin to READY state clears this
   * setting in output-selector */
  if (activepad) {
    GST_INFO_OBJECT (camera, "re-setting active pad in output-selector");

    g_object_set (G_OBJECT (camera->src_out_sel), "active-pad", activepad,
        NULL);
  }

  gst_element_set_state (GST_ELEMENT (camera), state);
}

/*
 * gst_camerabin_start_video_recording:
 * @camera: camerabin object
 *
 * Initiates video recording.
 */
static void
gst_camerabin_start_video_recording (GstCameraBin * camera)
{
  GstStateChangeReturn state_ret;
  GstCameraBinVideo *vidbin = (GstCameraBinVideo *) camera->vidbin;
  /* FIXME: how to ensure resolution and fps is supported by CPU?
   * use a queue overrun signal?
   */
  GST_INFO_OBJECT (camera, "starting video capture");

  /* check if need to update video capture caps */
  if (camera->video_capture_caps_update) {
    reset_video_capture_caps (camera);
  }

  gst_camerabin_rewrite_tags (camera);

  /* Pause the pipeline in order to distribute new clock in paused_to_playing */
  /* Audio source needs to go to null to reset the ringbuffer */
  if (vidbin->aud_src)
    gst_element_set_state (vidbin->aud_src, GST_STATE_NULL);
  state_ret = gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PAUSED);

  if (state_ret != GST_STATE_CHANGE_FAILURE) {
    GstClock *clock = gst_element_get_clock (GST_ELEMENT (camera));

    g_mutex_lock (camera->capture_mutex);
    camera->capturing = TRUE;
    g_mutex_unlock (camera->capture_mutex);
    gst_element_set_locked_state (camera->vidbin, FALSE);
    /* ensure elements activated before feeding data into it */
    state_ret = gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PAUSED);
    g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
        "active-pad", camera->pad_src_vid, NULL);

    /* Enable video mode in v4l2camsrc */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (camera->src_vid_src),
            "capture-mode")) {
      g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 2, NULL);
    }

    /* Clock might be distributed as NULL to audiosrc, messing timestamping */
    if (vidbin->aud_src)
      gst_element_set_clock (vidbin->aud_src, clock);
    gst_object_unref (clock);

    /* videobin will not go to playing if file is not writable */
    if (gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
      GST_ELEMENT_ERROR (camera, CORE, STATE_CHANGE,
          ("Setting videobin to PLAYING failed"), (NULL));
      gst_element_set_state (camera->vidbin, GST_STATE_NULL);
      gst_element_set_locked_state (camera->vidbin, TRUE);
      g_mutex_lock (camera->capture_mutex);
      camera->capturing = FALSE;
      g_mutex_unlock (camera->capture_mutex);
      gst_camerabin_reset_to_view_finder (camera);
    } else {
      gst_element_set_locked_state (camera->vidbin, TRUE);
    }
  } else {
    GST_WARNING_OBJECT (camera, "videobin state change failed");
    gst_element_set_state (camera->vidbin, GST_STATE_NULL);
    gst_camerabin_reset_to_view_finder (camera);

    CAMERABIN_PROCESSING_DEC (camera);
  }
}

/*
 * gst_camerabin_send_video_eos:
 * @camera: camerabin object
 *
 * Generate and send eos event to video bin in order to
 * finish recording properly.
 */
static void
gst_camerabin_send_video_eos (GstCameraBin * camera)
{
  GstPad *videopad;

  g_return_if_fail (camera != NULL);

  if (!camera->eos_handled) {
    /* Send eos event to video bin */
    GST_INFO_OBJECT (camera, "sending eos to videobin");
    videopad = gst_element_get_static_pad (camera->vidbin, "sink");
    gst_pad_send_event (videopad, gst_event_new_eos ());
    gst_object_unref (videopad);
    /* Block viewfinder after capturing if requested by application */
    GST_OBJECT_LOCK (camera);
    if (camera->block_viewfinder_trigger) {
      gst_pad_set_blocked_async (camera->pad_src_view, TRUE,
          (GstPadBlockCallback) camerabin_pad_blocked, camera);
    }
    GST_OBJECT_UNLOCK (camera);
    camera->eos_handled = TRUE;
  } else {
    GST_INFO_OBJECT (camera, "dropping duplicate EOS");
  }
}

/*
 * camerabin_pad_blocked:
 * @pad: pad to block/unblock
 * @blocked: TRUE to block, FALSE to unblock
 * @u_data: camera bin object
 *
 * Callback function for blocking a pad.
 */
static void
camerabin_pad_blocked (GstPad * pad, gboolean blocked, gpointer user_data)
{
  GstCameraBin *camera;

  camera = (GstCameraBin *) user_data;

  GST_DEBUG_OBJECT (camera, "%s %s:%s",
      blocked ? "blocking" : "unblocking", GST_DEBUG_PAD_NAME (pad));
}

/*
 * gst_camerabin_send_preview:
 * @camera: camerabin object
 * @buffer: received buffer
 *
 * Convert given buffer to desired preview format and send is as a #GstMessage
 * to application.
 *
 * Returns: TRUE always
 */
static gboolean
gst_camerabin_send_preview (GstCameraBin * camera, GstBuffer * buffer)
{
  GstCameraBinPreviewPipelineData *data;
  GstBuffer *prev = NULL;
  GstStructure *s;
  GstMessage *msg;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (camera, "creating preview");

  data = (camera->mode == MODE_IMAGE) ?
      camera->preview_pipeline : camera->video_preview_pipeline;
  prev = gst_camerabin_preview_convert (data, buffer);

  GST_DEBUG_OBJECT (camera, "preview created: %p", prev);

  if (prev) {
    s = gst_structure_new (PREVIEW_MESSAGE_NAME,
        "buffer", GST_TYPE_BUFFER, prev, NULL);
    gst_buffer_unref (prev);

    msg = gst_message_new_element (GST_OBJECT (camera), s);

    GST_DEBUG_OBJECT (camera, "sending message with preview image");

    if (gst_element_post_message (GST_ELEMENT (camera), msg) == FALSE) {
      GST_WARNING_OBJECT (camera,
          "This element has no bus, therefore no message sent!");
    }
    ret = TRUE;
  }

  return ret;
}

/*
 * gst_camerabin_have_img_buffer:
 * @pad: output-selector src pad leading to image bin
 * @buffer: still image frame
 * @u_data: camera bin object
 *
 * Buffer probe called before sending each buffer to image queue.
 * Generates and sends preview image as gst message if requested.
 */
static gboolean
gst_camerabin_have_img_buffer (GstPad * pad, GstMiniObject * obj,
    gpointer u_data)
{
  GstCameraBin *camera = (GstCameraBin *) u_data;

  if (GST_IS_BUFFER (obj)) {
    GstBuffer *buffer = GST_BUFFER_CAST (obj);
    GstStructure *fn_ev_struct = NULL;
    GstPad *os_sink = NULL;

    GST_LOG ("got buffer %p with size %d", buffer, GST_BUFFER_SIZE (buffer));

    if (camera->preview_caps) {
      gst_camerabin_send_preview (camera, buffer);
    }

    /* Image filename should be set by now */
    if (g_str_equal (camera->filename->str, "")) {
      GST_DEBUG_OBJECT (camera, "filename not set, dropping buffer");
      CAMERABIN_PROCESSING_DEC_UNLOCKED (camera);
      goto done;
    }

    gst_camerabin_rewrite_tags (camera);

    /* Send a custom event which tells the filename to image queue */
    /* NOTE: This needs to be THE FIRST event to be sent to queue for
       every image. It triggers imgbin state change to PLAYING. */
    fn_ev_struct = gst_structure_new ("img-filename",
        "filename", G_TYPE_STRING, camera->filename->str, NULL);
    GST_DEBUG_OBJECT (camera, "sending filename event to image queue");
    gst_camerabin_send_img_queue_custom_event (camera, fn_ev_struct);

    /* Add buffer probe to outputselector's sink pad. It sends
       EOS event to image queue. */
    os_sink = gst_element_get_static_pad (camera->src_out_sel, "sink");
    camera->image_captured_id = gst_pad_add_buffer_probe (os_sink,
        G_CALLBACK (gst_camerabin_have_src_buffer), camera);
    gst_object_unref (os_sink);

  done:

    /* HACK: v4l2camsrc changes to view finder resolution automatically
       after one captured still image */
    gst_camerabin_finish_image_capture (camera);

    GST_DEBUG_OBJECT (camera, "image captured, switching to viewfinder");

    gst_camerabin_reset_to_view_finder (camera);

    GST_DEBUG_OBJECT (camera, "switched back to viewfinder");

    return TRUE;
  } else if (GST_IS_EVENT (obj)) {
    GstEvent *event = GST_EVENT_CAST (obj);

    GST_DEBUG_OBJECT (camera, "Received event in image pipeline");

    /* forward tag events to preview pipeline */
    if (camera->preview_caps && GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
      GstCameraBinPreviewPipelineData *data;

      data = (camera->mode == MODE_IMAGE) ?
          camera->preview_pipeline : camera->video_preview_pipeline;
      gst_camerabin_preview_send_event (data, gst_event_ref (event));
    }
  }

  return TRUE;
}

/*
 * gst_camerabin_have_vid_buffer:
 * @pad: output-selector src pad leading to video bin
 * @buffer: buffer pushed to the pad
 * @u_data: camerabin object
 *
 * Buffer probe for src pad leading to video bin.
 * Sends eos event to video bin if stop requested and drops
 * all buffers after this.
 */
static gboolean
gst_camerabin_have_vid_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data)
{
  GstCameraBin *camera = (GstCameraBin *) u_data;
  gboolean ret = TRUE;
  GST_LOG ("got video buffer %p with size %d",
      buffer, GST_BUFFER_SIZE (buffer));

  if (!camera->video_preview_buffer && camera->video_preview_caps) {
    GST_DEBUG ("storing video preview %p", buffer);
    camera->video_preview_buffer = gst_buffer_copy (buffer);
  }

  if (G_UNLIKELY (camera->stop_requested)) {
    gst_camerabin_send_video_eos (camera);
    ret = FALSE;                /* Drop buffer */
  }

  return ret;
}

/*
 * gst_camerabin_have_src_buffer:
 * @pad: output-selector sink pad which receives frames from video source
 * @buffer: buffer pushed to the pad
 * @u_data: camerabin object
 *
 * Buffer probe for sink pad. It sends custom eos event to image queue and
 * notifies application by sending a "image-captured" message to GstBus.
 * This probe is installed after image has been captured and it disconnects
 * itself after EOS has been sent.
 */
static gboolean
gst_camerabin_have_src_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data)
{
  GstCameraBin *camera = (GstCameraBin *) u_data;
  GstMessage *msg;

  GST_LOG_OBJECT (camera, "got image buffer %p with size %d",
      buffer, GST_BUFFER_SIZE (buffer));

  g_mutex_lock (camera->capture_mutex);
  camera->capturing = FALSE;
  g_cond_signal (camera->cond);
  g_mutex_unlock (camera->capture_mutex);

  msg = gst_message_new_element (GST_OBJECT (camera),
      gst_structure_new (IMG_CAPTURED_MESSAGE_NAME, NULL));

  GST_DEBUG_OBJECT (camera, "sending 'image captured' message");

  if (gst_element_post_message (GST_ELEMENT (camera), msg) == FALSE) {
    GST_WARNING_OBJECT (camera,
        "This element has no bus, therefore no message sent!");
  }

  /* We can't send real EOS event, since it would switch the image queue
     into "draining mode". Therefore we send our own custom eos and
     catch & drop it later in queue's srcpad data probe */
  GST_DEBUG_OBJECT (camera, "sending img-eos to image queue");
  gst_camerabin_send_img_queue_custom_event (camera,
      gst_structure_new ("img-eos", NULL));

  /* Prevent video source from pushing frames until we want them */
  GST_OBJECT_LOCK (camera);
  if (camera->block_viewfinder_trigger) {
    gst_pad_set_blocked_async (camera->pad_src_view, TRUE,
        (GstPadBlockCallback) camerabin_pad_blocked, camera);
  }
  GST_OBJECT_UNLOCK (camera);

  /* our work is done, disconnect */
  gst_pad_remove_buffer_probe (pad, camera->image_captured_id);

  /* Image captured, notify that preparing a new capture is possible */
  g_object_notify (G_OBJECT (camera), "ready-for-capture");

  return TRUE;
}

/*
 * gst_camerabin_have_queue_data:
 * @pad: image queue src pad leading to image bin
 * @mini_obj: buffer or event pushed to the pad
 * @u_data: camerabin object
 *
 * Buffer probe for image queue src pad leading to image bin. It sets imgbin
 * into PLAYING mode when image buffer is passed to it. This probe also
 * monitors our internal custom events and handles them accordingly.
 */
static gboolean
gst_camerabin_have_queue_data (GstPad * pad, GstMiniObject * mini_obj,
    gpointer u_data)
{
  GstCameraBin *camera = (GstCameraBin *) u_data;
  gboolean ret = TRUE;

  if (GST_IS_BUFFER (mini_obj)) {
    GstEvent *tagevent;

    GST_LOG_OBJECT (camera, "queue sending image buffer to imagebin");

    tagevent = gst_event_new_tag (gst_tag_list_copy (camera->event_tags));
    gst_element_send_event (camera->imgbin, tagevent);
    gst_tag_list_free (camera->event_tags);
    camera->event_tags = gst_tag_list_new ();
  } else if (GST_IS_EVENT (mini_obj)) {
    const GstStructure *evs;
    GstEvent *event;

    event = GST_EVENT_CAST (mini_obj);
    evs = gst_event_get_structure (event);

    GST_LOG_OBJECT (camera, "got event %s", GST_EVENT_TYPE_NAME (event));

    if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
      GstTagList *tlist;

      GST_DEBUG_OBJECT (camera, "queue sending taglist to image pipeline");
      gst_event_parse_tag (event, &tlist);
      gst_tag_list_insert (camera->event_tags, tlist, GST_TAG_MERGE_REPLACE);
      ret = FALSE;
    } else if (evs && gst_structure_has_name (evs, "img-filename")) {
      const gchar *fname;

      GST_DEBUG_OBJECT (camera, "queue setting image filename to imagebin");
      fname = gst_structure_get_string (evs, "filename");
      g_object_set (G_OBJECT (camera->imgbin), "filename", fname, NULL);

      /* imgbin fails to start unless the filename is set or file
         cannot be written */
      if (gst_element_set_state (camera->imgbin, GST_STATE_PLAYING) ==
          GST_STATE_CHANGE_FAILURE) {
        GST_ELEMENT_ERROR (camera, CORE, STATE_CHANGE,
            ("Setting imagebin to PLAYING failed"), (NULL));
        gst_element_set_state (camera->imgbin, GST_STATE_NULL);
      } else {
        GST_LOG_OBJECT (camera, "Set imagebin to PLAYING");
      }

      ret = FALSE;
    } else if (evs && gst_structure_has_name (evs, "img-eos")) {
      GST_DEBUG_OBJECT (camera, "queue sending EOS to image pipeline");
      gst_pad_set_blocked_async (camera->pad_src_queue, TRUE,
          (GstPadBlockCallback) camerabin_pad_blocked, camera);
      gst_element_send_event (camera->imgbin, gst_event_new_eos ());
      ret = FALSE;
    }
  }

  return ret;
}

/*
 * gst_camerabin_reset_to_view_finder:
 * @camera: camerabin object
 *
 * Stop capturing and set camerabin to view finder mode.
 * Reset capture counters and flags.
 */
static void
gst_camerabin_reset_to_view_finder (GstCameraBin * camera)
{
  GstStateChangeReturn state_ret;
  GST_DEBUG_OBJECT (camera, "resetting");

  if (camera->src_out_sel) {
    /* Set selector to forward data to view finder */
    g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
        "active-pad", camera->pad_src_view, NULL);
  }

  /* Set video bin to READY state */
  if (camera->active_bin == camera->vidbin) {
    state_ret = gst_element_set_state (camera->active_bin, GST_STATE_READY);
    if (state_ret == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (camera, "state change failed");
      gst_element_set_state (camera->active_bin, GST_STATE_NULL);
      camera->active_bin = NULL;
    }
  }

  /* Reset counters and flags */
  camera->stop_requested = FALSE;
  camera->paused = FALSE;
  camera->eos_handled = FALSE;
  if (camera->video_preview_buffer) {
    gst_buffer_unref (camera->video_preview_buffer);
    camera->video_preview_buffer = NULL;
  }

  /* Enable view finder mode in v4l2camsrc */
  if (camera->src_vid_src &&
      g_object_class_find_property (G_OBJECT_GET_CLASS (camera->src_vid_src),
          "capture-mode")) {
    g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 0, NULL);
  }

  GST_DEBUG_OBJECT (camera, "reset done");
}

/*
 * gst_camerabin_do_stop:
 * @camera: camerabin object
 *
 * Raise flag to indicate to image and video bin capture stop.
 * Stopping paused video recording handled as a special case.
 * Wait for ongoing capturing to finish.
 */
static void
gst_camerabin_do_stop (GstCameraBin * camera)
{
  gboolean video_preview_sent = FALSE;
  g_mutex_lock (camera->capture_mutex);
  if (camera->capturing) {
    GST_DEBUG_OBJECT (camera, "mark stop");
    camera->stop_requested = TRUE;

    /* Post preview image ASAP and don't wait that video recording
       finishes as it may take time. */
    if (camera->video_preview_buffer) {
      gst_camerabin_send_preview (camera, camera->video_preview_buffer);
      video_preview_sent = TRUE;
    }

    /* Take special care when stopping paused video capture */
    if ((camera->active_bin == camera->vidbin) && camera->paused) {
      /* Send eos event to video bin before setting it to playing */
      gst_camerabin_send_video_eos (camera);
      /* We must change to playing now in order to get video bin eos events
         and buffered data through and finish recording properly */
      gst_element_set_state (GST_ELEMENT (camera->vidbin), GST_STATE_PLAYING);
      camera->paused = FALSE;
    }

    GST_DEBUG_OBJECT (camera, "waiting for capturing to finish");
    g_cond_wait (camera->cond, camera->capture_mutex);
    GST_DEBUG_OBJECT (camera, "capturing finished");

    if (camera->video_preview_buffer) {
      /* Double check that preview image has been sent. This is useful
         in a corner case where capture-stop is issued immediately after
         start before a single video buffer is actually recorded */
      if (video_preview_sent == FALSE) {
        gst_camerabin_send_preview (camera, camera->video_preview_buffer);
      }
    }
  }
  g_mutex_unlock (camera->capture_mutex);
}

/*
 * gst_camerabin_default_signal_img_done:
 * @camera: camerabin object
 * @fname: filename of the recently saved image
 *
 * Default handler for #GstCameraBin::image-done signal,
 * stops always capture.
 *
 * Returns: FALSE always
 */
static gboolean
gst_camerabin_default_signal_img_done (GstCameraBin * camera,
    const gchar * fname)
{
  return FALSE;
}

/*
 * gst_camerabin_set_allowed_framerate:
 * @camera: camerabin object
 * @filter_caps: update allowed framerate to these caps
 *
 * Find allowed frame rate from video source that matches with
 * resolution in @filter_caps. Set found frame rate to @filter_caps.
 */
static void
gst_camerabin_set_allowed_framerate (GstCameraBin * camera,
    GstCaps * filter_caps)
{
  GstStructure *structure;
  GstCaps *allowed_caps = NULL, *intersect = NULL, *tmp_caps = NULL;
  const GValue *framerate = NULL;
  guint caps_size, i;
  guint32 format = 0;

  GST_INFO_OBJECT (camera, "filter caps:%" GST_PTR_FORMAT, filter_caps);

  structure = gst_structure_copy (gst_caps_get_structure (filter_caps, 0));

  /* Set fourcc format according to current videosrc format */
  format = get_srcpad_current_format (camera->src_vid_src);
  if (format) {
    GST_DEBUG_OBJECT (camera,
        "using format %" GST_FOURCC_FORMAT " for matching",
        GST_FOURCC_ARGS (format));
    gst_structure_set (structure, "format", GST_TYPE_FOURCC, format, NULL);
  } else {
    GST_DEBUG_OBJECT (camera, "not matching against fourcc format");
    gst_structure_remove_field (structure, "format");
  }

  tmp_caps = gst_caps_new_full (structure, NULL);

  /* Get supported caps from video src that matches with new filter caps */
  allowed_caps = gst_camerabin_get_allowed_input_caps (camera);
  intersect = gst_caps_intersect (allowed_caps, tmp_caps);
  GST_INFO_OBJECT (camera, "intersect caps:%" GST_PTR_FORMAT, intersect);

  /* Find the best framerate from the caps */
  caps_size = gst_caps_get_size (intersect);
  for (i = 0; i < caps_size; i++) {
    structure = gst_caps_get_structure (intersect, i);
    framerate =
        gst_camerabin_find_better_framerate (camera, structure, framerate);
  }

  /* Set found frame rate to original caps */
  if (GST_VALUE_HOLDS_FRACTION (framerate)) {
    gst_caps_set_simple (filter_caps,
        "framerate", GST_TYPE_FRACTION,
        gst_value_get_fraction_numerator (framerate),
        gst_value_get_fraction_denominator (framerate), NULL);
  }

  /* Unref helper caps */
  if (allowed_caps) {
    gst_caps_unref (allowed_caps);
  }
  if (intersect) {
    gst_caps_unref (intersect);
  }
  if (tmp_caps) {
    gst_caps_unref (tmp_caps);
  }
}


/**
 * get_srcpad_current_format:
 * @element: element to get the format from
 *
 * Helper function to get the negotiated fourcc
 * format from @element src pad.
 *
 * Returns: negotiated format (fourcc), 0 if not found
 */
static guint32
get_srcpad_current_format (GstElement * element)
{
  GstPad *srcpad = NULL;
  GstCaps *srccaps = NULL;
  GstStructure *structure;
  guint32 format = 0;

  g_return_val_if_fail (element != NULL, 0);

  if ((srcpad = gst_element_get_static_pad (element, "src")) == NULL) {
    goto no_pad;
  }

  if ((srccaps = gst_pad_get_negotiated_caps (srcpad)) == NULL) {
    goto no_caps;
  }

  GST_LOG ("negotiated caps %" GST_PTR_FORMAT, srccaps);

  structure = gst_caps_get_structure (srccaps, 0);
  if (gst_structure_has_field (structure, "format")) {
    gst_structure_get_fourcc (structure, "format", &format);
  }

  gst_caps_unref (srccaps);
no_caps:
  gst_object_unref (srcpad);
no_pad:
  GST_DEBUG ("current format for %" GST_PTR_FORMAT ": %" GST_FOURCC_FORMAT,
      element, GST_FOURCC_ARGS (format));
  return format;
}

/*
 * gst_camerabin_find_better_framerate:
 * @camera: camerabin object
 * @st: structure that contains framerate candidates
 * @orig_framerate: best framerate so far
 *
 * Looks for framerate better than @orig_framerate from @st structure.
 * In night mode lowest framerate is considered best, otherwise highest is
 * best.
 *
 * Returns: @orig_framerate or better if found
 */
static const GValue *
gst_camerabin_find_better_framerate (GstCameraBin * camera, GstStructure * st,
    const GValue * orig_framerate)
{
  const GValue *framerate = NULL;
  guint i, i_best, list_size;
  gint res, comparison;

  if (camera->night_mode) {
    GST_LOG_OBJECT (camera, "finding min framerate in %" GST_PTR_FORMAT, st);
    comparison = GST_VALUE_LESS_THAN;
  } else {
    GST_LOG_OBJECT (camera, "finding max framerate in %" GST_PTR_FORMAT, st);
    comparison = GST_VALUE_GREATER_THAN;
  }

  if (gst_structure_has_field (st, "framerate")) {
    framerate = gst_structure_get_value (st, "framerate");
    /* Handle framerate lists */
    if (GST_VALUE_HOLDS_LIST (framerate)) {
      list_size = gst_value_list_get_size (framerate);
      GST_LOG_OBJECT (camera, "finding framerate from list");
      for (i = 0, i_best = 0; i < list_size; i++) {
        res = gst_value_compare (gst_value_list_get_value (framerate, i),
            gst_value_list_get_value (framerate, i_best));
        if (comparison == res) {
          i_best = i;
        }
      }
      GST_LOG_OBJECT (camera, "found best framerate from index %d", i_best);
      framerate = gst_value_list_get_value (framerate, i_best);
    }
    /* Handle framerate ranges */
    if (GST_VALUE_HOLDS_FRACTION_RANGE (framerate)) {
      if (camera->night_mode) {
        GST_LOG_OBJECT (camera, "getting min framerate from range");
        framerate = gst_value_get_fraction_range_min (framerate);
      } else {
        GST_LOG_OBJECT (camera, "getting max framerate from range");
        framerate = gst_value_get_fraction_range_max (framerate);
      }
    }
  }

  /* Check if we found better framerate */
  if (orig_framerate && framerate) {
    res = gst_value_compare (orig_framerate, framerate);
    if (comparison == res) {
      GST_LOG_OBJECT (camera, "original framerate was the best");
      framerate = orig_framerate;
    }
  }

  return framerate;
}

/*
 * gst_camerabin_update_aspect_filter:
 * @camera: camerabin object
 * @new_caps: new caps of next buffers arriving to view finder sink element
 *
 * Updates aspect ratio capsfilter to maintain aspect ratio, if we need to
 * scale frames for showing them in view finder.
 */
static void
gst_camerabin_update_aspect_filter (GstCameraBin * camera, GstCaps * new_caps)
{
  if (camera->flags & GST_CAMERABIN_FLAG_VIEWFINDER_SCALE) {
    GstCaps *sink_caps, *ar_caps;
    GstStructure *st;
    gint in_w = 0, in_h = 0, sink_w = 0, sink_h = 0, target_w = 0, target_h = 0;
    gdouble ratio_w, ratio_h;
    GstPad *sink_pad;
    const GValue *range;

    sink_pad = gst_element_get_static_pad (camera->view_sink, "sink");

    if (sink_pad) {
      sink_caps = gst_pad_get_caps (sink_pad);
      gst_object_unref (sink_pad);
      if (sink_caps) {
        if (!gst_caps_is_any (sink_caps)) {
          GST_DEBUG_OBJECT (camera, "sink element caps %" GST_PTR_FORMAT,
              sink_caps);
          /* Get maximum resolution that view finder sink accepts */
          st = gst_caps_get_structure (sink_caps, 0);
          if (gst_structure_has_field_typed (st, "width", GST_TYPE_INT_RANGE)) {
            range = gst_structure_get_value (st, "width");
            sink_w = gst_value_get_int_range_max (range);
          }
          if (gst_structure_has_field_typed (st, "height", GST_TYPE_INT_RANGE)) {
            range = gst_structure_get_value (st, "height");
            sink_h = gst_value_get_int_range_max (range);
          }
          GST_DEBUG_OBJECT (camera, "sink element accepts max %dx%d", sink_w,
              sink_h);

          /* Get incoming frames' resolution */
          if (sink_h && sink_w) {
            st = gst_caps_get_structure (new_caps, 0);
            gst_structure_get_int (st, "width", &in_w);
            gst_structure_get_int (st, "height", &in_h);
            GST_DEBUG_OBJECT (camera, "new caps with %dx%d", in_w, in_h);
          }
        }
        gst_caps_unref (sink_caps);
      }
    }

    /* If we get bigger frames than view finder sink accepts, then we scale.
       If we scale we need to adjust aspect ratio capsfilter caps in order
       to maintain aspect ratio while scaling. */
    if (in_w && in_h && (in_w > sink_w || in_h > sink_h)) {
      ratio_w = (gdouble) sink_w / in_w;
      ratio_h = (gdouble) sink_h / in_h;

      if (ratio_w < ratio_h) {
        target_w = sink_w;
        target_h = (gint) (ratio_w * in_h);
      } else {
        target_w = (gint) (ratio_h * in_w);
        target_h = sink_h;
      }

      GST_DEBUG_OBJECT (camera, "setting %dx%d filter to maintain aspect ratio",
          target_w, target_h);
      ar_caps = gst_caps_copy (new_caps);
      gst_caps_set_simple (ar_caps, "width", G_TYPE_INT, target_w, "height",
          G_TYPE_INT, target_h, NULL);
    } else {
      GST_DEBUG_OBJECT (camera, "no scaling");
      ar_caps = new_caps;
    }

    GST_DEBUG_OBJECT (camera, "aspect ratio filter caps %" GST_PTR_FORMAT,
        ar_caps);
    g_object_set (G_OBJECT (camera->aspect_filter), "caps", ar_caps, NULL);
    if (ar_caps != new_caps)
      gst_caps_unref (ar_caps);
  }
}

/*
 * gst_camerabin_finish_image_capture:
 * @camera: camerabin object
 *
 * Perform finishing operations after image capture is done and
 * returning back to view finder mode.
 */
static void
gst_camerabin_finish_image_capture (GstCameraBin * camera)
{
  if (camera->image_capture_caps) {
    /* If we used specific caps for image capture we need to 
       restore the caps and zoom/crop for view finder mode */
    if (camera->src_zoom_crop) {
      GST_DEBUG_OBJECT (camera, "resetting crop in camerabin");
      g_object_set (camera->src_zoom_crop, "left", 0, "right", 0,
          "top", 0, "bottom", 0, NULL);
    }
    camera->base_crop_left = 0;
    camera->base_crop_right = 0;
    camera->base_crop_top = 0;
    camera->base_crop_bottom = 0;
    gst_camerabin_set_capsfilter_caps (camera, camera->view_finder_caps);
  }
}

/*
 * gst_camerabin_adapt_image_capture:
 * @camera: camerabin object
 * @in_caps: caps object that describes incoming image format
 *
 * Adjust capsfilters and crop according image capture caps if necessary.
 * The captured image format from video source might be different from
 * what application requested, so we can try to fix that in camerabin.
 *
 */
static void
gst_camerabin_adapt_image_capture (GstCameraBin * camera, GstCaps * in_caps)
{
  GstStructure *in_st, *new_st, *req_st;
  gint in_width = 0, in_height = 0, req_width = 0, req_height = 0, crop = 0;
  gdouble ratio_w, ratio_h;
  GstCaps *filter_caps = NULL;

  GST_LOG_OBJECT (camera, "in caps: %" GST_PTR_FORMAT, in_caps);
  GST_LOG_OBJECT (camera, "requested caps: %" GST_PTR_FORMAT,
      camera->image_capture_caps);

  in_st = gst_caps_get_structure (in_caps, 0);
  gst_structure_get_int (in_st, "width", &in_width);
  gst_structure_get_int (in_st, "height", &in_height);

  req_st = gst_caps_get_structure (camera->image_capture_caps, 0);
  gst_structure_get_int (req_st, "width", &req_width);
  gst_structure_get_int (req_st, "height", &req_height);

  GST_INFO_OBJECT (camera, "we requested %dx%d, and got %dx%d", req_width,
      req_height, in_width, in_height);

  new_st = gst_structure_copy (req_st);
  /* If new fields have been added, we need to copy them */
  gst_structure_foreach (in_st, copy_missing_fields, new_st);

  if (!(camera->flags & GST_CAMERABIN_FLAG_SOURCE_RESIZE)) {
    GST_DEBUG_OBJECT (camera,
        "source-resize flag disabled, unable to adapt resolution");
    gst_structure_set (new_st, "width", G_TYPE_INT, in_width, "height",
        G_TYPE_INT, in_height, NULL);
  }

  GST_LOG_OBJECT (camera, "new image capture caps: %" GST_PTR_FORMAT, new_st);

  /* Crop if requested aspect ratio differs from incoming frame aspect ratio */
  if (camera->src_zoom_crop) {

    ratio_w = (gdouble) in_width / req_width;
    ratio_h = (gdouble) in_height / req_height;

    if (ratio_w < ratio_h) {
      crop = in_height - (req_height * ratio_w);
      camera->base_crop_top = crop / 2;
      camera->base_crop_bottom = crop / 2;
    } else {
      crop = in_width - (req_width * ratio_h);
      camera->base_crop_left = crop / 2;
      camera->base_crop_right += crop / 2;
    }

    GST_INFO_OBJECT (camera,
        "setting base crop: left:%d, right:%d, top:%d, bottom:%d",
        camera->base_crop_left, camera->base_crop_right, camera->base_crop_top,
        camera->base_crop_bottom);
    g_object_set (G_OBJECT (camera->src_zoom_crop), "top",
        camera->base_crop_top, "bottom", camera->base_crop_bottom, "left",
        camera->base_crop_left, "right", camera->base_crop_right, NULL);
  }

  /* Update capsfilters */
  gst_caps_replace (&camera->image_capture_caps,
      gst_caps_new_full (new_st, NULL));
  gst_camerabin_set_capsfilter_caps (camera, camera->image_capture_caps);

  /* Adjust the capsfilter before crop and videoscale elements if necessary */
  if (in_width == camera->width && in_height == camera->height) {
    GST_DEBUG_OBJECT (camera, "no adaptation with resolution needed");
  } else {
    GST_DEBUG_OBJECT (camera,
        "changing %" GST_PTR_FORMAT " from %dx%d to %dx%d", camera->src_filter,
        camera->width, camera->height, in_width, in_height);
    /* Apply the width and height to filter caps */
    g_object_get (G_OBJECT (camera->src_filter), "caps", &filter_caps, NULL);
    filter_caps = gst_caps_make_writable (filter_caps);
    gst_caps_set_simple (filter_caps, "width", G_TYPE_INT, in_width, "height",
        G_TYPE_INT, in_height, NULL);
    g_object_set (G_OBJECT (camera->src_filter), "caps", filter_caps, NULL);
    gst_caps_unref (filter_caps);
  }
}

/*
 * gst_camerabin_handle_scene_mode:
 * @camera:       camerabin object
 * scene_mode:    scene mode
 *
 * Handle scene mode if night mode was selected/deselected in video-source
 *
 */
static void
gst_camerabin_handle_scene_mode (GstCameraBin * camera, GstSceneMode scene_mode)
{
  if (scene_mode == GST_PHOTOGRAPHY_SCENE_MODE_NIGHT) {
    if (!camera->night_mode) {
      GST_DEBUG ("enabling night mode, lowering fps");
      /* Make camerabin select the lowest allowed frame rate */
      camera->night_mode = TRUE;
      /* Remember frame rate before setting night mode */
      camera->pre_night_fps_n = camera->fps_n;
      camera->pre_night_fps_d = camera->fps_d;
      do_set_video_resolution_fps (camera, camera->width, camera->height, 0, 1);
    } else {
      GST_DEBUG ("night mode already enabled");
    }
  } else {
    if (camera->night_mode) {
      GST_DEBUG ("disabling night mode, restoring fps to %d/%d",
          camera->pre_night_fps_n, camera->pre_night_fps_d);
      camera->night_mode = FALSE;
      do_set_video_resolution_fps (camera, camera->width, camera->height,
          camera->pre_night_fps_n, camera->pre_night_fps_d);
    }
  }
}

/*
 * gst_camerabin_scene_mode_notify_cb:
 * @video_source: videosrc object
 * @pspec:        GParamSpec for property
 * @user_data:    camerabin object
 *
 * Update framerate if scene mode was updated in video-source
 *
 */
static void
gst_camerabin_scene_mode_notify_cb (GObject * video_source, GParamSpec * pspec,
    gpointer user_data)
{
  GstSceneMode scene_mode;
  const gchar *name = g_param_spec_get_name (pspec);
  GstCameraBin *camera = GST_CAMERABIN (user_data);

  g_object_get (video_source, name, &scene_mode, NULL);
  gst_camerabin_handle_scene_mode (camera, scene_mode);
}

 /*
  * gst_camerabin_zoom_notify_cb:
  * @video_source: videosrc object
  * @pspec:        GParamSpec for property
  * @user_data:    camerabin object
  *
  * Update zoom value if video-source updated its zoom
  *
  */
static void
gst_camerabin_zoom_notify_cb (GObject * video_source, GParamSpec * pspec,
    gpointer user_data)
{
  gfloat zoom;
  const gchar *name = g_param_spec_get_name (pspec);
  GstCameraBin *camera = GST_CAMERABIN (user_data);

  g_object_get (video_source, name, &zoom, NULL);

  camera->zoom = zoom;
  g_object_notify (G_OBJECT (camera), "zoom");
}

/*
 * gst_camerabin_monitor_video_source_properties:
 * @camera: camerabin object
 *
 * Monitor notify signals from video source photography interface
 * property scene mode.
 *
 */
static void
gst_camerabin_monitor_video_source_properties (GstCameraBin * camera)
{
  GST_DEBUG_OBJECT (camera, "checking for photography interface support");
  if (GST_IS_ELEMENT (camera->src_vid_src) &&
      gst_element_implements_interface (camera->src_vid_src,
          GST_TYPE_PHOTOGRAPHY)) {
    gint scene_mode;
    GST_DEBUG_OBJECT (camera,
        "connecting to %" GST_PTR_FORMAT " - notify::scene-mode",
        camera->src_vid_src);
    g_signal_connect (G_OBJECT (camera->src_vid_src), "notify::scene-mode",
        (GCallback) gst_camerabin_scene_mode_notify_cb, camera);
    g_object_get (G_OBJECT (camera->src_vid_src), "scene-mode", &scene_mode,
        NULL);
    camera->night_mode = scene_mode == GST_PHOTOGRAPHY_SCENE_MODE_NIGHT;

    GST_DEBUG_OBJECT (camera,
        "connecting to %" GST_PTR_FORMAT " - notify::zoom",
        camera->src_vid_src);
    g_signal_connect (G_OBJECT (camera->src_vid_src), "notify::zoom",
        (GCallback) gst_camerabin_zoom_notify_cb, camera);
  }
}

/*
 * gst_camerabin_configure_format:
 * @camera: camerabin object
 * @caps: caps describing new format
 *
 * Configure internal video format for camerabin.
 *
 */
static void
gst_camerabin_configure_format (GstCameraBin * camera, GstCaps * caps)
{
  GstStructure *st;

  st = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (st, "width", &camera->width);
  gst_structure_get_int (st, "height", &camera->height);

  if (gst_structure_has_field_typed (st, "framerate", GST_TYPE_FRACTION)) {
    gst_structure_get_fraction (st, "framerate", &camera->fps_n,
        &camera->fps_d);
  }
}

static gboolean
copy_missing_fields (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstStructure *st = (GstStructure *) user_data;
  const GValue *val = gst_structure_id_get_value (st, field_id);

  if (G_UNLIKELY (val == NULL)) {
    gst_structure_id_set_value (st, field_id, value);
  }

  return TRUE;
}

/*
* gst_camerabin_change_viewfinder_blocking:
* @camera: camerabin object
* @blocked: new viewfinder blocking state
*
* Handle viewfinder blocking parameter change.
*/
static void
gst_camerabin_change_viewfinder_blocking (GstCameraBin * camera,
    gboolean blocked)
{
  gboolean old_value;

  GST_OBJECT_LOCK (camera);
  old_value = camera->block_viewfinder_prop;
  camera->block_viewfinder_prop = blocked;
  if (blocked == FALSE) {
    camera->block_viewfinder_trigger = FALSE;
  }
  GST_OBJECT_UNLOCK (camera);

  /* "block_viewfinder_prop" is now set and will be checked after capture */
  GST_DEBUG_OBJECT (camera, "viewfinder blocking set to %d, was %d",
      camera->block_viewfinder_prop, old_value);

  if (old_value == blocked)
    return;

  if (!blocked && camera->pad_src_view
      && gst_pad_is_blocked (camera->pad_src_view)) {
    /* Unblock viewfinder: the pad is blocked and we need to unblock it */
    gst_pad_set_blocked_async (camera->pad_src_view, FALSE,
        (GstPadBlockCallback) camerabin_pad_blocked, camera);
  }
}

/*
 * GObject callback functions implementation
 */

static void
gst_camerabin_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_tag_register_musicbrainz_tags ();

  gst_element_class_set_details_simple (element_class, "Camera Bin",
      "Generic/Bin/Camera",
      "Handle lot of features present in DSC",
      "Nokia Corporation <multimedia@maemo.org>, "
      "Edgard Lima <edgard.lima@indt.org.br>");
}

static void
gst_camerabin_class_init (GstCameraBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);

  /* gobject */

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_camerabin_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_camerabin_finalize);

  gobject_class->set_property = gst_camerabin_set_property;
  gobject_class->get_property = gst_camerabin_get_property;

  /**
   * GstCameraBin:filename:
   *
   * Set filename for the still image capturing or video capturing.
   */

  g_object_class_install_property (gobject_class, ARG_FILENAME,
      g_param_spec_string ("filename", "Filename",
          "Filename of the image or video to save", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:mode:
   *
   * Set the mode of operation: still image capturing or video recording.
   * Setting the mode will create and destroy image bin or video bin elements
   * according to the mode. You can set this property at any time, changing
   * the mode will stop ongoing capture.
   */

  g_object_class_install_property (gobject_class, ARG_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The capture mode (still image capture or video recording)",
          GST_TYPE_CAMERABIN_MODE, DEFAULT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:flags
   *
   * Control the behaviour of camerabin.
   */
  g_object_class_install_property (gobject_class, ARG_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Flags to control behaviour",
          GST_TYPE_CAMERABIN_FLAGS, DEFAULT_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstCameraBin:mute:
   *
   * Mute audio in video recording mode.
   * Set this property only when #GstCameraBin is in READY, PAUSED or PLAYING.
   */

  g_object_class_install_property (gobject_class, ARG_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "True to mute the recording. False to record with audio",
          ARG_DEFAULT_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:zoom:
   *
   * Set up the zoom applied to the frames.
   * Set this property only when #GstCameraBin is in READY, PAUSED or PLAYING.
   */

  g_object_class_install_property (gobject_class, ARG_ZOOM,
      g_param_spec_float ("zoom", "Zoom",
          "The zoom. 1.0 for 1x, 2.0 for 2x and so on",
          MIN_ZOOM, MAX_ZOOM, DEFAULT_ZOOM,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:image-post-processing:
   *
   * Set up an element to do image post processing.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */
  g_object_class_install_property (gobject_class, ARG_IMAGE_POST,
      g_param_spec_object ("image-post-processing",
          "Image post processing element",
          "Image Post-Processing GStreamer element (default is NULL)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:image-encoder:
   *
   * Set up an image encoder (for example, jpegenc or pngenc) element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_IMAGE_ENC,
      g_param_spec_object ("image-encoder", "Image encoder",
          "Image encoder GStreamer element (default is jpegenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:image-formatter:
   *
   * Set up an image formatter (for example, jifmux) element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_IMAGE_FORMATTER,
      g_param_spec_object ("image-formatter", "Image formatter",
          "Image formatter GStreamer element (default is jifmux)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:video-post-processing:
   *
   * Set up an element to do video post processing.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_POST,
      g_param_spec_object ("video-post-processing",
          "Video post processing element",
          "Video post processing GStreamer element (default is NULL)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:video-encoder:
   *
   * Set up a video encoder element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_ENC,
      g_param_spec_object ("video-encoder", "Video encoder",
          "Video encoder GStreamer element (default is theoraenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:audio-encoder:
   *
   * Set up an audio encoder element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_AUDIO_ENC,
      g_param_spec_object ("audio-encoder", "Audio encoder",
          "Audio encoder GStreamer element (default is vorbisenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:video-muxer:
   *
   * Set up a video muxer element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_MUX,
      g_param_spec_object ("video-muxer", "Video muxer",
          "Video muxer GStreamer element (default is oggmux)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:viewfinder-sink:
   *
   * Set up a sink element to render frames in view finder.
   * By default "autovideosink" or DEFAULT_VIDEOSINK will be used.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VF_SINK,
      g_param_spec_object ("viewfinder-sink", "Viewfinder sink",
          "Viewfinder sink GStreamer element (NULL = default video sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:video-source:
   *
   * Set up a video source element.
   * By default "autovideosrc" or DEFAULT_VIDEOSRC will be used.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_SRC,
      g_param_spec_object ("video-source", "Video source element",
          "Video source GStreamer element (NULL = default video src)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   *  GstCameraBin:audio-source:
   *
   * Set up an audio source element.
   * By default "autoaudiosrc" or DEFAULT_AUDIOSRC will be used.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_AUDIO_SRC,
      g_param_spec_object ("audio-source", "Audio source element",
          "Audio source GStreamer element (NULL = default audio src)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   *  GstCameraBin:video-source-filter:
   *
   * Set up optional video filter element, all frames from video source
   * will be processed by this element. e.g. An application might add
   * image enhancers/parameter adjustment filters here to improve captured
   * image/video results, or add analyzers to give feedback on capture
   * the application.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_SOURCE_FILTER,
      g_param_spec_object ("video-source-filter", "video source filter element",
          "Optional video filter GStreamer element, filters all frames from"
          "the video source", GST_TYPE_ELEMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:video-source-caps:
   *
   * The allowed modes of operation of the video source. Have in mind that it
   * doesn't mean #GstCameraBin can operate in all those modes,
   * it depends also on the other elements in the pipeline. Remember to
   * gst_caps_unref after using it.
   */

  g_object_class_install_property (gobject_class, ARG_INPUT_CAPS,
      g_param_spec_boxed ("video-source-caps", "Video source caps",
          "The allowed modes of the video source operation",
          GST_TYPE_CAPS, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:filter-caps:
   *
   * Caps applied to capsfilter element after videosrc [ ! ffmpegcsp ].
   * You can use this e.g. to make sure video color format matches with
   * encoders and other elements configured to camerabin and/or change
   * resolution and frame rate.
   */

  g_object_class_install_property (gobject_class, ARG_FILTER_CAPS,
      g_param_spec_boxed ("filter-caps", "Filter caps",
          "Filter video data coming from videosrc element",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:preview-caps:
   *
   * If application wants to receive a preview image, it needs to 
   * set this property to depict the desired image format caps. When
   * this property is not set (NULL), message containing the preview
   * image is not sent.
   */

  g_object_class_install_property (gobject_class, ARG_PREVIEW_CAPS,
      g_param_spec_boxed ("preview-caps", "Preview caps",
          "Caps defining the preview image format",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:preview-source-filter:
   * Set up preview filter element, all frames coming from appsrc
   * element will be processed by this element.
   * Applications can use this to overlay text/images for preview frame, 
   * for example.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_PREVIEW_SOURCE_FILTER,
      g_param_spec_object ("preview-source-filter",
          "preview source filter element",
          "Optional preview source filter GStreamer element",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:viewfinder-filter:
   * Set up viewfinder filter element, all frames going to viewfinder sink
   * element will be processed by this element.
   * Applications can use this to overlay text/images in the screen, or
   * plug facetracking algorithms, for example.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIEWFINDER_FILTER,
      g_param_spec_object ("viewfinder-filter", "viewfinder filter element",
          "viewfinder filter GStreamer element",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:block-after-capture:
   *
   * Block viewfinder after capture.
   * If it is TRUE when 'capture-start' is issued, camerabin will prepare to
   * block and freeze the viewfinder after capturing. Setting it to FALSE will
   * abort the blocking if it hasn't happened yet, or will enable again the
   * viewfinder if it is already blocked. Note that setting this property
   * to TRUE after 'capture-start' will only work for the next capture. This
   * makes possible for applications to set the property to FALSE to abort
   * the current blocking and already set it back to TRUE again to block at
   * the next capture.
   *
   * This is useful if application wants to display the preview image
   * and running the viewfinder at the same time would be just a waste of
   * CPU cycles.
   */

  g_object_class_install_property (gobject_class, ARG_BLOCK_VIEWFINDER,
      g_param_spec_boolean ("block-after-capture",
          "Block viewfinder after capture",
          "Block viewfinder after capturing an image or video",
          DEFAULT_BLOCK_VIEWFINDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /**
    * GstCameraBin:image-capture-width:
    *
    * The width to be used when capturing still images. If 0, the
    * viewfinder's width will be used.
    */
  g_object_class_install_property (gobject_class, ARG_IMAGE_CAPTURE_WIDTH,
      g_param_spec_int ("image-capture-width",
          "The width used for image capture",
          "The width used for image capture", 0, G_MAXINT16,
          DEFAULT_CAPTURE_WIDTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /**
    * GstCameraBin:image-capture-height:
    *
    * The height to be used when capturing still images. If 0, the
    * viewfinder's height will be used.
    */
  g_object_class_install_property (gobject_class, ARG_IMAGE_CAPTURE_HEIGHT,
      g_param_spec_int ("image-capture-height",
          "The height used for image capture",
          "The height used for image capture", 0, G_MAXINT16,
          DEFAULT_CAPTURE_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /**
    * GstCameraBin:video-capture-width:
    *
    * The width to be used when capturing video.
    */
  g_object_class_install_property (gobject_class, ARG_VIDEO_CAPTURE_WIDTH,
      g_param_spec_int ("video-capture-width",
          "The width used for video capture",
          "The width used for video capture", 0, G_MAXINT16,
          DEFAULT_CAPTURE_WIDTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:video-capture-height:
   *
   * The height to be used when capturing video.
   */
  g_object_class_install_property (gobject_class, ARG_VIDEO_CAPTURE_HEIGHT,
      g_param_spec_int ("video-capture-height",
          "The height used for video capture",
          "The height used for video capture", 0, G_MAXINT16,
          DEFAULT_CAPTURE_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:video-capture-framerate:
   *
   * The framerate to be used when capturing video.
   */
  g_object_class_install_property (gobject_class, ARG_VIDEO_CAPTURE_FRAMERATE,
      gst_param_spec_fraction ("video-capture-framerate",
          "The framerate used for video capture",
          "The framerate used for video capture", 0, 1, G_MAXINT32, 1,
          DEFAULT_FPS_N, DEFAULT_FPS_D,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:ready-for-capture:
   *
   * When TRUE new capture can be prepared. If FALSE capturing is ongoing
   * and starting a new capture immediately is not possible.
   */

  g_object_class_install_property (gobject_class, ARG_READY_FOR_CAPTURE,
      g_param_spec_boolean ("ready-for-capture",
          "Indicates if preparing a new capture is possible",
          "Indicates if preparing a new capture is possible",
          DEFAULT_READY_FOR_CAPTURE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin:idle:
   *
   * When TRUE no capturing/encoding/saving is running and it is safe to set
   * camerabin to NULL to release resources without losing data.
   *
   * In case of errors, this property is made unreliable. Set the pipeline
   * back to READY or NULL to make it reliable again.
   */
  g_object_class_install_property (gobject_class, ARG_IDLE,
      g_param_spec_boolean ("idle",
          "Indicates if data is being processed (recording/capturing/saving)",
          "Indicates if data is being processed (recording/capturing/saving)",
          TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCameraBin::capture-start:
   * @camera: the camera bin element
   *
   * Starts image capture or video recording depending on the Mode.
   * If there is a capture already going on, does nothing.
   * Resumes video recording if it has been paused.
   */

  camerabin_signals[CAPTURE_START_SIGNAL] =
      g_signal_new ("capture-start",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, capture_start),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::capture-stop:
   * @camera: the camera bin element
   *
   * Stops still image preview, continuous image capture and video
   * recording and returns to the view finder mode.
   */

  camerabin_signals[CAPTURE_STOP_SIGNAL] =
      g_signal_new ("capture-stop",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, capture_stop),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::capture-pause:
   * @camera: the camera bin element
   *
   * Pauses video recording or resumes paused video recording.
   * If in image mode or not recording, does nothing.
   */

  camerabin_signals[CAPTURE_PAUSE_SIGNAL] =
      g_signal_new ("capture-pause",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, capture_pause),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::set-video-resolution-fps:
   * @camera: the camera bin element
   * @width: number of horizontal pixels
   * @height: number of vertical pixels
   * @fps_n: frames per second numerator
   * @fps_d: frames per second denominator
   *
   * Changes the frame resolution and frames per second of the video source.
   * The application must be aware of the resolutions supported by the camera.
   * Supported resolutions and frame rates can be get using input-caps property.
   *
   * Setting @fps_n or @fps_d to 0 configures maximum framerate for the
   * given resolution, unless in night mode when minimum is configured.
   *
   * This is the same as setting the 'video-capture-width',
   * 'video-capture-height' and 'video-capture-framerate' properties, but it
   * already updates the caps to force use this resolution and framerate.
   */

  camerabin_signals[SET_VIDEO_RESOLUTION_FPS_SIGNAL] =
      g_signal_new ("set-video-resolution-fps",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, set_video_resolution_fps),
      NULL, NULL, __gst_camerabin_marshal_VOID__INT_INT_INT_INT, G_TYPE_NONE, 4,
      G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

  /**
   * GstCameraBin::set-image-resolution:
   * @camera: the camera bin element
   * @width: number of horizontal pixels
   * @height: number of vertical pixels
   *
   * Changes the resolution used for still image capture.
   * Does not affect view finder mode and video recording.
   *
   * This actually sets the 'image-capture-width' and 'image-capture-height'
   * properties.
   */

  camerabin_signals[SET_IMAGE_RESOLUTION_SIGNAL] =
      g_signal_new ("set-image-resolution",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, set_image_resolution),
      NULL, NULL, __gst_camerabin_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
      G_TYPE_INT, G_TYPE_INT);

  /**
   * GstCameraBin::image-done:
   * @camera: the camera bin element
   * @filename: the name of the file just saved
   *
   * Signal emitted when the file has just been saved.
   *
   * Don't call any #GstCameraBin method from this signal, if you do so there
   * will be a deadlock.
   */

  camerabin_signals[IMG_DONE_SIGNAL] =
      g_signal_new ("image-done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstCameraBinClass, img_done),
      g_signal_accumulator_true_handled, NULL,
      __gst_camerabin_marshal_BOOLEAN__STRING, G_TYPE_BOOLEAN, 1,
      G_TYPE_STRING);

  klass->capture_start = gst_camerabin_capture_start;
  klass->capture_stop = gst_camerabin_capture_stop;
  klass->capture_pause = gst_camerabin_capture_pause;
  klass->set_video_resolution_fps = gst_camerabin_set_video_resolution_fps;
  klass->set_image_resolution = gst_camerabin_set_image_resolution;

  klass->img_done = gst_camerabin_default_signal_img_done;

  /* gstelement */

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_camerabin_change_state);

  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_camerabin_provide_clock);

  /* gstbin */
  /* override handle_message to peek when video or image bin reaches eos */
  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gst_camerabin_handle_message_func);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_camerabin_init (GstCameraBin * camera, GstCameraBinClass * gclass)
{
  /* GstElementClass *klass = GST_ELEMENT_GET_CLASS (camera); */

  camera->filename = g_string_new ("");
  camera->mode = DEFAULT_MODE;
  camera->flags = DEFAULT_FLAGS;
  camera->stop_requested = FALSE;
  camera->paused = FALSE;
  camera->capturing = FALSE;
  camera->night_mode = FALSE;
  camera->eos_handled = FALSE;

  camera->app_width = camera->width = DEFAULT_WIDTH;
  camera->app_height = camera->height = DEFAULT_HEIGHT;
  camera->app_fps_n = camera->fps_n = DEFAULT_FPS_N;
  camera->app_fps_d = camera->fps_d = DEFAULT_FPS_D;
  camera->image_capture_width = 0;
  camera->image_capture_height = 0;
  camera->base_crop_left = 0;
  camera->base_crop_right = 0;
  camera->base_crop_top = 0;
  camera->base_crop_bottom = 0;

  camera->event_tags = gst_tag_list_new ();

  camera->image_capture_caps = NULL;
  camera->view_finder_caps = NULL;
  camera->allowed_caps = NULL;

  camera->zoom = DEFAULT_ZOOM;

  /* concurrency control */
  camera->capture_mutex = g_mutex_new ();
  camera->cond = g_cond_new ();
  camera->idle_cond = g_cond_new ();
  camera->processing_counter = 0;

  /* pad names for output and input selectors */
  camera->pad_src_view = NULL;
  camera->pad_view_src = NULL;
  camera->pad_src_img = NULL;
  camera->pad_src_vid = NULL;
  camera->pad_view_vid = NULL;

  camera->video_preview_buffer = NULL;
  camera->preview_caps = NULL;
  camera->video_preview_caps = NULL;

  /* image capture bin */
  camera->imgbin = g_object_new (GST_TYPE_CAMERABIN_IMAGE, NULL);
  gst_object_ref (camera->imgbin);

  /* video capture bin */
  camera->vidbin = g_object_new (GST_TYPE_CAMERABIN_VIDEO, NULL);
  gst_object_ref (camera->vidbin);

  /* view finder elements */
  camera->view_in_sel = NULL;
  camera->view_scale = NULL;
  camera->aspect_filter = NULL;
  camera->view_sink = NULL;

  camera->app_vf_sink = NULL;
  camera->app_viewfinder_filter = NULL;

  /* preview elements */
  camera->app_preview_source_filter = NULL;
  camera->app_video_preview_source_filter = NULL;

  /* source elements */
  camera->src_vid_src = NULL;
  camera->src_filter = NULL;
  camera->src_zoom_crop = NULL;
  camera->src_zoom_scale = NULL;
  camera->src_zoom_filter = NULL;
  camera->src_out_sel = NULL;

  camera->app_video_filter = NULL;
  camera->app_vid_src = NULL;

  camera->active_bin = NULL;
}

static void
gst_camerabin_dispose (GObject * object)
{
  GstCameraBin *camera;

  camera = GST_CAMERABIN (object);

  GST_DEBUG_OBJECT (camera, "disposing");

  gst_element_set_state (camera->imgbin, GST_STATE_NULL);
  gst_object_unref (camera->imgbin);

  gst_element_set_state (camera->vidbin, GST_STATE_NULL);
  gst_object_unref (camera->vidbin);

  if (camera->preview_pipeline) {
    gst_camerabin_preview_destroy_pipeline (camera->preview_pipeline);
    camera->preview_pipeline = NULL;
  }
  if (camera->video_preview_pipeline) {
    gst_camerabin_preview_destroy_pipeline (camera->video_preview_pipeline);
    camera->video_preview_pipeline = NULL;
  }

  camerabin_destroy_elements (camera);
  camerabin_dispose_elements (camera);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_camerabin_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_camerabin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCameraBin *camera = GST_CAMERABIN (object);

  switch (prop_id) {
    case ARG_MUTE:
      gst_camerabin_video_set_mute (GST_CAMERABIN_VIDEO (camera->vidbin),
          g_value_get_boolean (value));
      break;
    case ARG_ZOOM:
      camera->zoom = g_value_get_float (value);
      /* does not set it if in NULL, the src is not created yet */
      if (GST_STATE (camera) != GST_STATE_NULL)
        gst_camerabin_setup_zoom (camera);
      break;
    case ARG_MODE:
      gst_camerabin_change_mode (camera, g_value_get_enum (value));
      break;
    case ARG_FLAGS:
      gst_camerabin_set_flags (camera, g_value_get_flags (value));
      break;
    case ARG_FILENAME:
      gst_camerabin_change_filename (camera, g_value_get_string (value));
      break;
    case ARG_VIDEO_POST:
      if (GST_STATE (camera->vidbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next video bin NULL to READY state change");
      }
      gst_camerabin_video_set_post (GST_CAMERABIN_VIDEO (camera->vidbin),
          g_value_get_object (value));
      break;
    case ARG_VIDEO_ENC:
      if (GST_STATE (camera->vidbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next video bin NULL to READY state change");
      }
      gst_camerabin_video_set_video_enc (GST_CAMERABIN_VIDEO (camera->vidbin),
          g_value_get_object (value));
      break;
    case ARG_AUDIO_ENC:
      if (GST_STATE (camera->vidbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next video bin NULL to READY state change");
      }
      gst_camerabin_video_set_audio_enc (GST_CAMERABIN_VIDEO (camera->vidbin),
          g_value_get_object (value));
      break;
    case ARG_VIDEO_MUX:
      if (GST_STATE (camera->vidbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next video bin NULL to READY state change");
      }
      gst_camerabin_video_set_muxer (GST_CAMERABIN_VIDEO (camera->vidbin),
          g_value_get_object (value));
      break;
    case ARG_IMAGE_POST:
      if (GST_STATE (camera->imgbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next image bin NULL to READY state change");
      }
      gst_camerabin_image_set_postproc (GST_CAMERABIN_IMAGE (camera->imgbin),
          g_value_get_object (value));
      break;
    case ARG_IMAGE_ENC:
      if (GST_STATE (camera->imgbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next image bin NULL to READY state change");
      }
      gst_camerabin_image_set_encoder (GST_CAMERABIN_IMAGE (camera->imgbin),
          g_value_get_object (value));
      break;
    case ARG_IMAGE_FORMATTER:
      if (GST_STATE (camera->imgbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next image bin NULL to READY state change");
      }
      gst_camerabin_image_set_formatter (GST_CAMERABIN_IMAGE (camera->imgbin),
          g_value_get_object (value));
      break;
    case ARG_VF_SINK:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the view finder element"),
            (NULL));
      } else {
        if (camera->app_vf_sink)
          gst_object_unref (camera->app_vf_sink);
        camera->app_vf_sink = g_value_get_object (value);
        if (camera->app_vf_sink)
          gst_object_ref (camera->app_vf_sink);
      }
      break;
    case ARG_VIDEO_SRC:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the video source element"),
            (NULL));
      } else {
        if (camera->app_vid_src)
          gst_object_unref (camera->app_vid_src);
        camera->app_vid_src = g_value_get_object (value);
        if (camera->app_vid_src)
          gst_object_ref (camera->app_vid_src);
      }
      break;
    case ARG_AUDIO_SRC:
      if (GST_STATE (camera->vidbin) != GST_STATE_NULL) {
        GST_WARNING_OBJECT (camera,
            "can't use set element until next video bin NULL to READY state change");
      }
      gst_camerabin_video_set_audio_src (GST_CAMERABIN_VIDEO (camera->vidbin),
          g_value_get_object (value));
      break;
    case ARG_VIDEO_SOURCE_FILTER:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the video filter element"),
            (NULL));
      } else {
        if (camera->app_video_filter)
          gst_object_unref (camera->app_video_filter);
        camera->app_video_filter = g_value_dup_object (value);
      }
      break;
    case ARG_FILTER_CAPS:
      GST_OBJECT_LOCK (camera);
      gst_caps_replace (&camera->view_finder_caps,
          (GstCaps *) gst_value_get_caps (value));
      GST_OBJECT_UNLOCK (camera);
      if (!camera->view_finder_caps)
        camera->view_finder_caps =
            gst_caps_from_string (CAMERABIN_DEFAULT_VF_CAPS);
      gst_camerabin_configure_format (camera, camera->view_finder_caps);
      break;
    case ARG_PREVIEW_CAPS:
    {
      GstCameraBinPreviewPipelineData **prev_pipe = NULL;
      GstElement **preview_source_filter = NULL;
      GstCaps **prev_caps = NULL;
      GstCaps *new_caps = NULL;

      if (camera->mode == MODE_IMAGE) {
        prev_pipe = &camera->preview_pipeline;
        preview_source_filter = &camera->app_preview_source_filter;
        prev_caps = &camera->preview_caps;
      } else {                  /* MODE VIDEO */
        prev_pipe = &camera->video_preview_pipeline;
        preview_source_filter = &camera->app_video_preview_source_filter;
        prev_caps = &camera->video_preview_caps;
      }

      new_caps = (GstCaps *) gst_value_get_caps (value);

      if (prev_caps && !gst_caps_is_equal (*prev_caps, new_caps)) {
        GST_DEBUG_OBJECT (camera,
            "setting preview caps: %" GST_PTR_FORMAT, new_caps);

        GST_OBJECT_LOCK (camera);
        gst_caps_replace (prev_caps, new_caps);
        GST_OBJECT_UNLOCK (camera);

        if (new_caps && !gst_caps_is_any (new_caps) &&
            !gst_caps_is_empty (new_caps)) {
          if (!*prev_pipe) {
            *prev_pipe =
                gst_camerabin_preview_create_pipeline (GST_ELEMENT (camera),
                new_caps, *preview_source_filter);
          } else {
            gst_camerabin_preview_set_caps (*prev_pipe, new_caps);
          }
        }
      }
      break;
    }
    case ARG_PREVIEW_SOURCE_FILTER:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the preview source filter element"),
            (NULL));
      } else {
        GstCameraBinPreviewPipelineData **preview_pipe = NULL;
        GstElement **preview_source_filter = NULL;
        GstCaps *preview_caps = NULL;

        if (camera->mode == MODE_IMAGE) {
          preview_pipe = &camera->preview_pipeline;
          preview_source_filter = &camera->app_preview_source_filter;
          preview_caps = camera->preview_caps;
        } else {                /* MODE VIDEO */
          preview_pipe = &camera->video_preview_pipeline;
          preview_source_filter = &camera->app_video_preview_source_filter;
          preview_caps = camera->video_preview_caps;
        }

        if (*preview_source_filter)
          gst_object_unref (*preview_source_filter);
        *preview_source_filter = g_value_dup_object (value);

        if (*preview_pipe) {
          gst_camerabin_preview_destroy_pipeline (*preview_pipe);
          *preview_pipe =
              gst_camerabin_preview_create_pipeline (GST_ELEMENT (camera),
              preview_caps, *preview_source_filter);
        }
      }
      break;
    case ARG_VIEWFINDER_FILTER:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the viewfinder filter element"),
            (NULL));
      } else {
        if (camera->app_viewfinder_filter)
          gst_object_unref (camera->app_viewfinder_filter);
        camera->app_viewfinder_filter = g_value_dup_object (value);
      }
      break;
    case ARG_BLOCK_VIEWFINDER:
      gst_camerabin_change_viewfinder_blocking (camera,
          g_value_get_boolean (value));
      break;
    case ARG_IMAGE_CAPTURE_WIDTH:
    {
      gint width = g_value_get_int (value);

      if (width != camera->image_capture_width) {
        camera->image_capture_width = width;
        camera->image_capture_caps_update = TRUE;
      }
    }
      break;
    case ARG_IMAGE_CAPTURE_HEIGHT:
    {
      gint height = g_value_get_int (value);

      if (height != camera->image_capture_height) {
        camera->image_capture_height = height;
        camera->image_capture_caps_update = TRUE;
      }
    }
      break;
    case ARG_VIDEO_CAPTURE_WIDTH:
    {
      gint width = g_value_get_int (value);

      camera->app_width = width;

      if (width != camera->width) {
        camera->width = width;
        camera->video_capture_caps_update = TRUE;
      }
    }
      break;
    case ARG_VIDEO_CAPTURE_HEIGHT:
    {
      gint height = g_value_get_int (value);

      camera->app_height = height;

      if (height != camera->height) {
        camera->height = height;
        camera->video_capture_caps_update = TRUE;
      }
    }
      break;
    case ARG_VIDEO_CAPTURE_FRAMERATE:
    {
      gint fps_n, fps_d;

      fps_n = gst_value_get_fraction_numerator (value);
      fps_d = gst_value_get_fraction_denominator (value);

      camera->app_fps_n = fps_n;
      camera->app_fps_d = fps_d;

      if (fps_n != camera->fps_n || fps_d != camera->fps_d) {
        camera->fps_n = fps_n;
        camera->fps_d = fps_d;
        camera->video_capture_caps_update = TRUE;
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camerabin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraBin *camera = GST_CAMERABIN (object);

  switch (prop_id) {
    case ARG_FILENAME:
      g_value_set_string (value, camera->filename->str);
      break;
    case ARG_MODE:
      g_value_set_enum (value, camera->mode);
      break;
    case ARG_FLAGS:
      g_value_set_flags (value, camera->flags);
      break;
    case ARG_MUTE:
      g_value_set_boolean (value,
          gst_camerabin_video_get_mute (GST_CAMERABIN_VIDEO (camera->vidbin)));
      break;
    case ARG_ZOOM:
      g_value_set_float (value, camera->zoom);
      break;
    case ARG_IMAGE_POST:
      g_value_set_object (value,
          gst_camerabin_image_get_postproc (GST_CAMERABIN_IMAGE
              (camera->imgbin)));
      break;
    case ARG_IMAGE_ENC:
      g_value_set_object (value,
          gst_camerabin_image_get_encoder (GST_CAMERABIN_IMAGE
              (camera->imgbin)));
      break;
    case ARG_IMAGE_FORMATTER:
      g_value_set_object (value,
          gst_camerabin_image_get_formatter (GST_CAMERABIN_IMAGE
              (camera->imgbin)));
      break;
    case ARG_VIDEO_POST:
      g_value_set_object (value,
          gst_camerabin_video_get_post (GST_CAMERABIN_VIDEO (camera->vidbin)));
      break;
    case ARG_VIDEO_ENC:
      g_value_set_object (value,
          gst_camerabin_video_get_video_enc (GST_CAMERABIN_VIDEO
              (camera->vidbin)));
      break;
    case ARG_AUDIO_ENC:
      g_value_set_object (value,
          gst_camerabin_video_get_audio_enc (GST_CAMERABIN_VIDEO
              (camera->vidbin)));
      break;
    case ARG_VIDEO_MUX:
      g_value_set_object (value,
          gst_camerabin_video_get_muxer (GST_CAMERABIN_VIDEO (camera->vidbin)));
      break;
    case ARG_VF_SINK:
      if (camera->view_sink)
        g_value_set_object (value, camera->view_sink);
      else
        g_value_set_object (value, camera->app_vf_sink);
      break;
    case ARG_VIDEO_SRC:
      if (camera->src_vid_src)
        g_value_set_object (value, camera->src_vid_src);
      else
        g_value_set_object (value, camera->app_vid_src);
      break;
    case ARG_AUDIO_SRC:
      g_value_set_object (value,
          gst_camerabin_video_get_audio_src (GST_CAMERABIN_VIDEO
              (camera->vidbin)));
      break;
    case ARG_VIDEO_SOURCE_FILTER:
      g_value_set_object (value, camera->app_video_filter);
      break;
    case ARG_INPUT_CAPS:
      gst_value_set_caps (value, gst_camerabin_get_allowed_input_caps (camera));
      break;
    case ARG_FILTER_CAPS:
      gst_value_set_caps (value, camera->view_finder_caps);
      break;
    case ARG_PREVIEW_CAPS:
      if (camera->mode == MODE_IMAGE)
        gst_value_set_caps (value, camera->preview_caps);
      else if (camera->mode == MODE_VIDEO)
        gst_value_set_caps (value, camera->video_preview_caps);
      break;
    case ARG_PREVIEW_SOURCE_FILTER:
      if (camera->mode == MODE_IMAGE)
        g_value_set_object (value, camera->app_preview_source_filter);
      else if (camera->mode == MODE_VIDEO)
        g_value_set_object (value, camera->app_video_preview_source_filter);
      break;
    case ARG_VIEWFINDER_FILTER:
      g_value_set_object (value, camera->app_viewfinder_filter);
      break;
    case ARG_BLOCK_VIEWFINDER:
      g_value_set_boolean (value, camera->block_viewfinder_prop);
      break;
    case ARG_READY_FOR_CAPTURE:
      g_mutex_lock (camera->capture_mutex);
      g_value_set_boolean (value, !camera->capturing);
      g_mutex_unlock (camera->capture_mutex);
      break;
    case ARG_IMAGE_CAPTURE_WIDTH:
      g_value_set_int (value, camera->image_capture_width);
      break;
    case ARG_IMAGE_CAPTURE_HEIGHT:
      g_value_set_int (value, camera->image_capture_height);
      break;
    case ARG_VIDEO_CAPTURE_WIDTH:
      g_value_set_int (value, camera->app_width);
      break;
    case ARG_VIDEO_CAPTURE_HEIGHT:
      g_value_set_int (value, camera->app_height);
      break;
    case ARG_VIDEO_CAPTURE_FRAMERATE:
      gst_value_set_fraction (value, camera->app_fps_n, camera->app_fps_d);
      break;
    case ARG_IDLE:
      g_value_set_boolean (value, camera->processing_counter == 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
 * GstElement functions implementation
 */

static GstStateChangeReturn
gst_camerabin_change_state (GstElement * element, GstStateChange transition)
{
  GstCameraBin *camera = GST_CAMERABIN (element);
  GstStateChangeReturn ret;

  GST_DEBUG_OBJECT (element, "changing state: %s -> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!camerabin_create_elements (camera)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      /* Lock to control image and video bin state separately
         from view finder */
      gst_element_set_locked_state (camera->imgbin, TRUE);
      gst_element_set_locked_state (camera->vidbin, TRUE);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      camerabin_setup_src_elements (camera);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* If using autovideosink, set view finder sink properties
         now that actual sink has been created. */
      camerabin_setup_view_elements (camera);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* all processing should stop and those elements could have their state
       * locked, so set them explicitly here */
      if (GST_STATE (camera->imgbin) != GST_STATE_NULL) {
        gst_element_set_state (camera->imgbin, GST_STATE_READY);
      }
      if (GST_STATE (camera->vidbin) != GST_STATE_NULL) {
        gst_element_set_state (camera->vidbin, GST_STATE_READY);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_element_set_locked_state (camera->imgbin, FALSE);
      gst_element_set_locked_state (camera->vidbin, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  GST_DEBUG_OBJECT (element, "after chaining up: %s -> %s = %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)),
      gst_element_state_change_return_get_name (ret));

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (camera->capture_mutex);
      if (camera->capturing) {
        GST_WARNING_OBJECT (camera, "was capturing when changing to READY");
        camera->capturing = FALSE;
        /* Reset capture and don't wait for capturing to finish properly.
           Proper capturing should have been finished before going to READY. */
        gst_camerabin_reset_to_view_finder (camera);
        g_cond_signal (camera->cond);
      }

      /* reset processing counter */
      GST_DEBUG_OBJECT (camera, "Reset processing counter from %d to 0",
          camera->processing_counter);
      camera->processing_counter = 0;
      g_cond_signal (camera->idle_cond);
      g_object_notify (G_OBJECT (camera), "idle");
      g_mutex_unlock (camera->capture_mutex);

      /* unblock the viewfinder, but keep the property as is */
      gst_pad_set_blocked_async (camera->pad_src_view, FALSE,
          (GstPadBlockCallback) camerabin_pad_blocked, camera);

      g_signal_handlers_disconnect_by_func (camera->src_vid_src,
          gst_camerabin_scene_mode_notify_cb, camera);
      g_signal_handlers_disconnect_by_func (camera->src_vid_src,
          gst_camerabin_zoom_notify_cb, camera);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      camerabin_destroy_elements (camera);
      break;
      /* In some error situation camerabin may end up being still in NULL
         state so we must take care of destroying elements. */
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (ret == GST_STATE_CHANGE_FAILURE)
        camerabin_destroy_elements (camera);
      break;
    default:
      break;
  }

done:
  GST_DEBUG_OBJECT (element, "changed state: %s -> %s = %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)),
      gst_element_state_change_return_get_name (ret));

  return ret;
}

static GstClock *
gst_camerabin_provide_clock (GstElement * element)
{
  GstClock *clock = NULL;
  GstClock *vidbin_clock = NULL;
  GstCameraBin *camera = GST_CAMERABIN (element);
  GstElement *aud_src = GST_CAMERABIN_VIDEO (camera->vidbin)->aud_src;

  if (aud_src)
    vidbin_clock = gst_element_provide_clock (aud_src);

  if (camera->capturing && camera->mode == MODE_VIDEO && vidbin_clock)
    clock = vidbin_clock;
  else {
    clock = GST_ELEMENT_CLASS (parent_class)->provide_clock (element);
    if (clock == vidbin_clock) {
      /* Do not reuse vidbin_clock if it was current clock */
      clock = gst_system_clock_obtain ();
    }
  }

  GST_INFO_OBJECT (camera, "Reset pipeline clock to %p(%s)",
      clock, GST_ELEMENT_NAME (clock));

  return clock;
}

static gpointer
gst_camerabin_imgbin_finished (gpointer u_data)
{
  GstCameraBin *camera = GST_CAMERABIN (u_data);
  gchar *filename = NULL;

  /* FIXME: should set a flag (and take a lock) when going to NULL, so we
   * short-circuit this bit if we got shut down between thread create and now */

  GST_DEBUG_OBJECT (camera, "Image encoding finished");

  /* Get the filename of the finished image */
  g_object_get (G_OBJECT (camera->imgbin), "filename", &filename, NULL);

  /* Close the file of saved image */
  gst_element_set_state (camera->imgbin, GST_STATE_READY);
  GST_DEBUG_OBJECT (camera, "Image pipeline set to READY");

  g_mutex_lock (camera->capture_mutex);
  if (camera->processing_counter) {
    CAMERABIN_PROCESSING_DEC_UNLOCKED (camera);
  } else {
    /* Camerabin state change to READY may have reset processing counter to
     * zero. This is possible as this functions is scheduled from another
     * thread.
     */
    GST_WARNING_OBJECT (camera, "camerabin has been forced to idle");
  }
  g_mutex_unlock (camera->capture_mutex);

  /* Set image bin back to PAUSED so that buffer-allocs don't fail */
  gst_element_set_state (camera->imgbin, GST_STATE_PAUSED);

  /* Unblock image queue pad to process next buffer */
  GST_STATE_LOCK (camera);
  if (camera->pad_src_queue) {
    gst_pad_set_blocked_async (camera->pad_src_queue, FALSE,
        (GstPadBlockCallback) camerabin_pad_blocked, camera);
    GST_DEBUG_OBJECT (camera, "Queue srcpad unblocked");
  } else {
    GST_DEBUG_OBJECT (camera, "Queue srcpad unreffed already, doesn't need "
        "to unblock");
  }
  GST_STATE_UNLOCK (camera);

  /* Send image-done signal */
  gst_camerabin_image_capture_continue (camera, filename);
  g_free (filename);

  GST_INFO_OBJECT (camera, "leaving helper thread");
  gst_object_unref (camera);
  return NULL;
}

/*
 * GstBin functions implementation
 */

/* Peek eos messages but don't interfere with bin msg handling */
static void
gst_camerabin_handle_message_func (GstBin * bin, GstMessage * msg)
{
  GstCameraBin *camera = GST_CAMERABIN (bin);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (camera->vidbin)) {
        /* Video eos */
        GST_DEBUG_OBJECT (camera,
            "got video eos message, stopping video capture");
        g_mutex_lock (camera->capture_mutex);
        camera->capturing = FALSE;
        g_cond_signal (camera->cond);

        CAMERABIN_PROCESSING_DEC_UNLOCKED (camera);
        g_mutex_unlock (camera->capture_mutex);
      } else if (GST_MESSAGE_SRC (msg) == GST_OBJECT (camera->imgbin)) {
        /* Image eos */
        GST_DEBUG_OBJECT (camera, "got image eos message");
        /* Can't change state here, since we're in the streaming thread */
        if (!g_thread_create (gst_camerabin_imgbin_finished,
                gst_object_ref (camera), FALSE, NULL)) {
          /* FIXME: what do do if this fails? */
          gst_object_unref (camera);
        }
      }
      break;
    case GST_MESSAGE_ERROR:
      GST_DEBUG_OBJECT (camera, "error from child %" GST_PTR_FORMAT,
          GST_MESSAGE_SRC (msg));
      g_mutex_lock (camera->capture_mutex);
      if (camera->capturing) {
        camera->capturing = FALSE;
        g_cond_signal (camera->cond);
      }

      /* Ideally we should check what error was and only decrement the
       * counter if the error means that a 'processing' operation failed,
       * instead of a setting up error. But this can be quite tricky to do
       * and we expect the app to set the whole pipeline to READY/NULL
       * when an error happens. For now we just mention that the
       * processing counter and the 'idle' property are unreliable */
      GST_DEBUG_OBJECT (camera, "An error makes the processing counter "
          "unreliable");

      g_mutex_unlock (camera->capture_mutex);
      break;
    default:
      break;
  }
  GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

/*
 * Action signal function implementation
 */

static void
gst_camerabin_capture_start (GstCameraBin * camera)
{

  GST_INFO_OBJECT (camera, "starting capture");
  if (camera->paused) {
    gst_camerabin_capture_pause (camera);
    return;
  }

  if (!camera->active_bin) {
    GST_INFO_OBJECT (camera, "mode not explicitly set by application");
    gst_camerabin_change_mode (camera, camera->mode);
    if (!camera->active_bin) {
      GST_ELEMENT_ERROR (camera, CORE, FAILED,
          ("starting capture failed"), (NULL));
    }
  }

  /* We need a filename unless it's a photo and preview_caps is set */

  if (g_str_equal (camera->filename->str, ""))
    if (camera->active_bin == camera->vidbin || !camera->preview_caps) {
      GST_ELEMENT_ERROR (camera, CORE, FAILED,
          ("set filename before starting capture"), (NULL));
      return;
    }

  g_mutex_lock (camera->capture_mutex);
  if (camera->capturing) {
    GST_WARNING_OBJECT (camera, "capturing \"%s\" ongoing, set new filename",
        camera->filename->str);
    /* FIXME: we need to send something more to the app, so that it does not for
     * for image-done */
    g_mutex_unlock (camera->capture_mutex);
    return;
  }
  CAMERABIN_PROCESSING_INC_UNLOCKED (camera);
  g_mutex_unlock (camera->capture_mutex);

  GST_OBJECT_LOCK (camera);
  camera->block_viewfinder_trigger = camera->block_viewfinder_prop;
  GST_OBJECT_UNLOCK (camera);

  if (camera->active_bin) {
    if (camera->active_bin == camera->imgbin) {
      GST_INFO_OBJECT (camera, "starting image capture");
      gst_camerabin_start_image_capture (camera);
    } else if (camera->active_bin == camera->vidbin) {
      GST_INFO_OBJECT (camera,
          "setting video filename and starting video capture");
      g_object_set (G_OBJECT (camera->active_bin), "filename",
          camera->filename->str, NULL);
      gst_camerabin_start_video_recording (camera);
    }
  }
  /* Capturing is now ongoing, notify that new capture isn't possible */
  g_object_notify (G_OBJECT (camera), "ready-for-capture");
}

static void
gst_camerabin_capture_stop (GstCameraBin * camera)
{
  if (camera->active_bin == camera->vidbin) {
    GST_INFO_OBJECT (camera, "stopping video capture");
    gst_camerabin_do_stop (camera);
    gst_camerabin_reset_to_view_finder (camera);
    /* Video capture stopped, notify that preparing a new capture is possible */
    g_object_notify (G_OBJECT (camera), "ready-for-capture");
  } else {
    GST_INFO_OBJECT (camera, "stopping image capture isn't needed");
  }
}

static void
gst_camerabin_capture_pause (GstCameraBin * camera)
{
  if (camera->active_bin == camera->vidbin) {
    if (!camera->paused) {
      GST_INFO_OBJECT (camera, "pausing capture");

      /* Bring all camerabin elements to PAUSED */
      gst_element_set_locked_state (camera->vidbin, FALSE);
      gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PAUSED);

      /* Switch to view finder mode */
      g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
          "active-pad", camera->pad_src_view, NULL);

      /* Set view finder to PLAYING and leave videobin PAUSED */
      gst_element_set_locked_state (camera->vidbin, TRUE);
      gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING);

      camera->paused = TRUE;
    } else {
      GST_INFO_OBJECT (camera, "unpausing capture");

      /* Bring all camerabin elements to PAUSED */
      gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PAUSED);

      /* Switch to video recording mode */
      g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", TRUE,
          "active-pad", camera->pad_src_vid, NULL);

      /* Bring all camerabin elements to PLAYING */
      gst_element_set_locked_state (camera->vidbin, FALSE);
      gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING);
      gst_element_set_locked_state (camera->vidbin, TRUE);

      camera->paused = FALSE;
    }
    GST_DEBUG_OBJECT (camera, "pause done");
  } else {
    GST_WARNING ("pausing in image capture mode disabled");
  }
}

/*
 * Updates the properties (excluding the user preferred width/height/fps) and
 * tells camerabin to update the video capture caps.
 */
static void
do_set_video_resolution_fps (GstCameraBin * camera, gint width,
    gint height, gint fps_n, gint fps_d)
{
  if (height != camera->height) {
    camera->height = height;
    camera->video_capture_caps_update = TRUE;
  }
  if (width != camera->width) {
    camera->width = width;
    camera->video_capture_caps_update = TRUE;
  }
  if (fps_n != camera->fps_n) {
    camera->fps_n = fps_n;
    camera->video_capture_caps_update = TRUE;
  }
  if (fps_d != camera->fps_d) {
    camera->fps_d = fps_d;
    camera->video_capture_caps_update = TRUE;
  }

  reset_video_capture_caps (camera);
}

/*
 * Updates the properties (including the user preferred width/height/fps) and
 * tells camerabin to update the video capture caps.
 */
static void
gst_camerabin_set_video_resolution_fps (GstCameraBin * camera, gint width,
    gint height, gint fps_n, gint fps_d)
{
  g_object_set (camera, "video-capture-width", width, "video-capture-height",
      height, "video-capture-framerate", fps_n, fps_d, NULL);

  reset_video_capture_caps (camera);
}

static void
gst_camerabin_set_image_capture_caps (GstCameraBin * camera, gint width,
    gint height)
{
  GstStructure *structure;
  GstCaps *new_caps = NULL;

  g_return_if_fail (camera != NULL);

  if (width && height && camera->view_finder_caps) {
    /* Use view finder mode caps as a basis */
    structure = gst_caps_get_structure (camera->view_finder_caps, 0);

    /* Set new resolution for image capture */
    new_caps = gst_caps_new_simple (gst_structure_get_name (structure),
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

    /* Set allowed framerate for the resolution. */
    gst_camerabin_set_allowed_framerate (camera, new_caps);
  }

  GST_INFO_OBJECT (camera,
      "init filter caps for image capture %" GST_PTR_FORMAT, new_caps);
  gst_caps_replace (&camera->image_capture_caps, new_caps);
  camera->image_capture_caps_update = FALSE;
  if (new_caps)
    gst_caps_unref (new_caps);
}

static void
gst_camerabin_set_image_resolution (GstCameraBin * camera, gint width,
    gint height)
{
  g_object_set (camera, "image-capture-width", (guint16) width,
      "image-capture-height", (guint16) height, NULL);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_camerabin_debug, "camerabin", 0, "CameraBin");

  return gst_element_register (plugin, "camerabin",
      GST_RANK_NONE, GST_TYPE_CAMERABIN);
}

/* this is the structure that gstreamer looks for to register plugins
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "camerabin",
    "High level api for DC (Digital Camera) application",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
