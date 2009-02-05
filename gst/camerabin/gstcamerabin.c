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
 * SECTION:gstcamerabin
 * @short_description: camera capture bin
 *
 * <refsect2>
 * <para>
 * GstCameraBin is a high-level camera object that encapsulates the gstreamer
 * internals and provides a task based API for the application. It consists of
 * three main data paths: view-finder, image capture and video capture.
 * </para>
 * <informalfigure>
 *   <mediaobject>
 *     <imageobject><imagedata fileref="camerabin.png"/></imageobject>
 *     <textobject><phrase>CameraBin structure</phrase></textobject>
 *     <caption><para>Structural decomposition of CameraBin object.</para></caption>
 *   </mediaobject>
 * </informalfigure>
 * </refsect2>
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m camerabin filename=test.jpeg
 * </programlisting>
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Image capture</title>
 * <para>
 * Taking still images is initiated with the #GstCameraBin::user-start action
 * signal. Once the image has captured, #GstCameraBin::img-done signal is fired.
 * It allows to decide wheter to take another picture (burst capture, bracketing
 * shot) or stop capturing. The last captured image is shown
 * until one switches back to view finder using #GstCameraBin::user-stop action
 * signal.
 * </para>
 * <para>
 * Available resolutions can be taken from the #GstCameraBin:inputcaps property.
 * Image capture resolution can be set with #GstCameraBin::user-image-res
 * action signal.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Video capture</title>
 * <para>
 * Video capture is started with the #GstCameraBin::user-start action signal too.
 * In addition to image capture one can use #GstCameraBin::user-pause to
 * pause recording and #GstCameraBin::user-stop to end recording.
 * </para>
 * <para>
 * Available resolutions and fps can be taken from the #GstCameraBin:inputcaps
 * property. #GstCameraBin::user-res-fps action signal can be used to set frame
 * rate and resolution for the video recording and view finder as well.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Photography interface</title>
 * <para>
 * GstCameraBin implements gst photography interface, which can be used to set
 * and get different settings related to digital imaging. Since currently many
 * of these settings require low-level support the photography interface support
 * is dependent on video src element. In practice photography interface settings
 * cannot be used successfully until in PAUSED state when the video src has
 * opened the video device.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>States</title>
 * <para>
 * Elements within GstCameraBin are created and destroyed when switching
 * between NULL and READY states. Therefore element properties should be set
 * in NULL state. User set elements are not unreffed until GstCameraBin is
 * unreffed or replaced by a new user set element. Initially only elements needed
 * for view finder mode are created to speed up startup. Image bin and video bin
 * elements are created when setting the mode or starting capture.
 * </para>
 * </refsect2>
 * <refsect2>
 * <note>
 * <para>
 * Since the muxers tested so far have problems with discontinous buffers, QoS
 * has been disabled, and then in order to record video, you MUST ensure that
 * there is enough CPU to encode the video. Thus choose smart resolution and
 * frames per second values. It is also highly recommended to avoid color
 * conversions; make sure all the elements involved work with the same colorspace
 * (i.e. rgb or yuv i420 or whatelse).
 * </para>
 * </note>
 * </refsect2>
 */

/*
 * The pipeline in the camerabin is
 *
 *                                   "image bin"
 * videosrc ! crop ! scale ! out-sel <------> in-sel ! scale ! ffmpegcsp ! vfsink
 *                                   "video bin"
 *
 * it is possible to have 'ffmpegcolorspace' and 'capsfilter' just after
 * v4l2camsrc
 *
 * The properties of elements are:
 *
 *   vfsink - "sync", FALSE, "qos", FALSE
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
/* FIXME: include #include <gst/gst-i18n-plugin.h> and use _(" ") */

#include "gstcamerabin.h"
#include "gstcamerabinxoverlay.h"
#include "gstcamerabincolorbalance.h"
#include "gstcamerabinphotography.h"

#include "camerabingeneral.h"

#include "gstcamerabin-marshal.h"

/*
 * enum and types
 */

enum
{
  /* action signals */
  USER_START_SIGNAL,
  USER_STOP_SIGNAL,
  USER_PAUSE_SIGNAL,
  USER_RES_FPS_SIGNAL,
  USER_IMAGE_RES_SIGNAL,
  /* emit signals */
  IMG_DONE_SIGNAL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FILENAME,
  ARG_MODE,
  ARG_MUTE,
  ARG_ZOOM,
  ARG_IMAGE_POST,
  ARG_IMAGE_ENC,
  ARG_VIDEO_POST,
  ARG_VIDEO_ENC,
  ARG_AUDIO_ENC,
  ARG_VIDEO_MUX,
  ARG_VF_SINK,
  ARG_VIDEO_SRC,
  ARG_AUDIO_SRC,
  ARG_INPUT_CAPS,
  ARG_FILTER_CAPS
};

/*
 * defines and static global vars
 */

static guint camerabin_signals[LAST_SIGNAL];

#define GST_TYPE_CAMERABIN_MODE (gst_camerabin_mode_get_type ())

/* default and range values for args */

#define DEFAULT_MODE MODE_IMAGE
#define DEFAULT_ZOOM 100
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_CAPTURE_WIDTH 800
#define DEFAULT_CAPTURE_HEIGHT 600
#define DEFAULT_FPS_N 0         /* makes it use the default */
#define DEFAULT_FPS_D 1
#define CAMERABIN_DEFAULT_VF_CAPS "video/x-raw-yuv,format=(fourcc)I420"
/* Using "bilinear" as default zoom method */
#define CAMERABIN_DEFAULT_ZOOM_METHOD 1

#define MIN_ZOOM 100
#define MAX_ZOOM 1000
#define ZOOM_1X MIN_ZOOM

#define DEFAULT_V4L2CAMSRC_DRIVER_NAME "omap3cam"

/* internal element names */

#define USE_COLOR_CONVERTER 1

/* FIXME: use autovideosrc
 * v4l2camsrc should have GST_RANK_PRIMARY in maemo and _NONE in plugins-bad
 * for now */
static const char SRC_VID_SRC[] = "v4l2camsrc";
/*static const char SRC_VID_SRC[] = "v4l2src"; */

static const char ZOOM_CROP[] = "videocrop";
static const char ZOOM_SCALE[] = "videoscale";

static const char CAPS_FILTER[] = "capsfilter";

static const char COLOR_CONVERTER[] = "ffmpegcolorspace";

static const char SRC_OUT_SEL[] = "output-selector";

static const char VIEW_IN_SEL[] = "input-selector";
static const char VIEW_SCALE[] = "videoscale";
static const char VIEW_SINK[] = "autovideosink";

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
gst_camerabin_change_filename (GstCameraBin * camera, const gchar * name);

static void gst_camerabin_setup_zoom (GstCameraBin * camera);

static GstCaps *gst_camerabin_get_allowed_input_caps (GstCameraBin * camera);

static void gst_camerabin_rewrite_tags (GstCameraBin * camera);

static void
gst_camerabin_set_capsfilter_caps (GstCameraBin * camera, GstCaps * new_caps);

static void gst_camerabin_start_image_capture (GstCameraBin * camera);

static void gst_camerabin_start_video_recording (GstCameraBin * camera);

static gboolean
gst_camerabin_have_img_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data);
static gboolean
gst_camerabin_have_vid_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data);

static void gst_camerabin_reset_to_view_finder (GstCameraBin * camera);

static void gst_camerabin_do_stop (GstCameraBin * camera);

static void
gst_camerabin_set_allowed_framerate (GstCameraBin * camera,
    GstCaps * filter_caps);

/*
 * GObject callback functions declaration
 */

static void gst_camerabin_base_init (gpointer gclass);

static void gst_camerabin_class_init (GstCameraBinClass * klass);

static void
gst_camerabin_init (GstCameraBin * camera, GstCameraBinClass * gclass);

static void gst_camerabin_dispose (GObject * object);

static void gst_camerabin_finalize (GObject * object);

static void gst_camerabin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_camerabin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static const GValue *gst_camerabin_find_better_framerate (GstCameraBin * camera,
    GstStructure * st, const GValue * orig_framerate);
/*
 * GstElement function declarations
 */

static GstStateChangeReturn
gst_camerabin_change_state (GstElement * element, GstStateChange transition);


/*
 * GstBin function declarations
 */
static void
gst_camerabin_handle_message_func (GstBin * bin, GstMessage * message);


/*
 * Action signal function declarations
 */

static void gst_camerabin_user_start (GstCameraBin * camera);

static void gst_camerabin_user_stop (GstCameraBin * camera);

static void gst_camerabin_user_pause (GstCameraBin * camera);

static void
gst_camerabin_user_res_fps (GstCameraBin * camera, gint width, gint height,
    gint fps_n, gint fps_d);

static void
gst_camerabin_user_image_res (GstCameraBin * camera, gint width, gint height);


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

  if (iface_type == GST_TYPE_X_OVERLAY) {
    if (camera->view_sink) {
      return GST_IS_X_OVERLAY (camera->view_sink);
    }
  } else if (iface_type == GST_TYPE_COLOR_BALANCE) {
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
  } else if (iface_type == GST_TYPE_PHOTOGRAPHY) {
    if (camera->src_vid_src) {
      return GST_IS_PHOTOGRAPHY (camera->src_vid_src);
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

  static const GInterfaceInfo camerabin_xoverlay_info = {
    (GInterfaceInitFunc) gst_camerabin_xoverlay_init,
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
  static const GInterfaceInfo camerabin_photography_info = {
    (GInterfaceInitFunc) gst_camerabin_photography_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &camerabin_info);

  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY,
      &camerabin_xoverlay_info);

  g_type_add_interface_static (type, GST_TYPE_COLOR_BALANCE,
      &camerabin_color_balance_info);

  g_type_add_interface_static (type, GST_TYPE_TAG_SETTER,
      &camerabin_tagsetter_info);

  g_type_add_interface_static (type, GST_TYPE_PHOTOGRAPHY,
      &camerabin_photography_info);
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

  if (!camera->view_finder_caps) {
    st = gst_structure_from_string (CAMERABIN_DEFAULT_VF_CAPS, NULL);
  } else {
    st = gst_structure_copy (gst_caps_get_structure (camera->view_finder_caps,
            0));
  }

  if (camera->width > 0 && camera->height > 0) {
    gst_structure_set (st,
        "width", G_TYPE_INT, camera->width,
        "height", G_TYPE_INT, camera->height, NULL);
  }

  if (camera->fps_n > 0 && camera->fps_d > 0) {
    if (camera->night_mode) {
      GST_WARNING_OBJECT (camera,
          "night mode, lowest allowed fps will be forced");
      camera->pre_night_fps_n = camera->fps_n;
      camera->pre_night_fps_d = camera->fps_d;
      detect_framerate = TRUE;
    } else {
      gst_structure_set (st,
          "framerate", GST_TYPE_FRACTION, camera->fps_n, camera->fps_d, NULL);
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
  g_object_set (camera->src_zoom_scale, "method",
      CAMERABIN_DEFAULT_ZOOM_METHOD, NULL);

  gst_caps_replace (&camera->view_finder_caps, new_caps);

  /* Set caps for view finder mode */
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

  if (camera->user_vid_src) {
    camera->src_vid_src = camera->user_vid_src;

    if (!gst_camerabin_add_element (cbin, camera->src_vid_src)) {
      camera->src_vid_src = NULL;
      goto done;
    }
  } else if (!(camera->src_vid_src =
          gst_camerabin_create_and_add_element (cbin, SRC_VID_SRC)))
    goto done;
#ifdef USE_COLOR_CONVERTER
  if (!gst_camerabin_create_and_add_element (cbin, COLOR_CONVERTER))
    goto done;
#endif
  if (!(camera->src_filter =
          gst_camerabin_create_and_add_element (cbin, CAPS_FILTER)))
    goto done;
  if (!(camera->src_zoom_crop =
          gst_camerabin_create_and_add_element (cbin, ZOOM_CROP)))
    goto done;
  if (!(camera->src_zoom_scale =
          gst_camerabin_create_and_add_element (cbin, ZOOM_SCALE)))
    goto done;
  if (!(camera->src_zoom_filter =
          gst_camerabin_create_and_add_element (cbin, CAPS_FILTER)))
    goto done;
  if (!(camera->src_out_sel =
          gst_camerabin_create_and_add_element (cbin, SRC_OUT_SEL)))
    goto done;

  camera->srcpad_zoom_filter =
      gst_element_get_static_pad (camera->src_zoom_filter, "src");

  /* Set default "driver-name" for v4l2camsrc if not set */
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
    for (child = children; child != NULL; child = g_list_next (children)) {
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

  if (!(camera->view_in_sel =
          gst_camerabin_create_and_add_element (GST_BIN (camera),
              VIEW_IN_SEL))) {
    goto error;
  }

  /* Look for recently added input selector sink pad, we need to release it later */
  pads = GST_ELEMENT_PADS (camera->view_in_sel);
  while (pads != NULL
      && (GST_PAD_DIRECTION (GST_PAD (pads->data)) != GST_PAD_SINK)) {
    pads = g_list_next (pads);
  }
  camera->pad_view_img = GST_PAD (pads->data);

  if (!(camera->view_scale =
          gst_camerabin_create_and_add_element (GST_BIN (camera),
              VIEW_SCALE))) {
    goto error;
  }
#ifdef USE_COLOR_CONVERTER
  if (!gst_camerabin_create_and_add_element (GST_BIN (camera), COLOR_CONVERTER)) {
    goto error;
  }
#endif
  if (camera->user_vf_sink) {
    camera->view_sink = camera->user_vf_sink;
    if (!gst_camerabin_add_element (GST_BIN (camera), camera->view_sink)) {
      goto error;
    }
  } else if (!(camera->view_sink =
          gst_camerabin_create_and_add_element (GST_BIN (camera), VIEW_SINK))) {
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

  GST_LOG_OBJECT (camera, "creating elems");

  /* Create "src" elements */
  if (!camerabin_create_src_elements (camera)) {
    goto done;
  }

  /* Add image bin */
  camera->pad_src_img =
      gst_element_get_request_pad (camera->src_out_sel, "src%d");
  if (!gst_camerabin_add_element (GST_BIN (camera), camera->imgbin)) {
    goto done;
  }
  gst_pad_add_buffer_probe (camera->pad_src_img,
      G_CALLBACK (gst_camerabin_have_img_buffer), camera);

  /* Create view finder elements, this also links it to image bin */
  if (!camerabin_create_view_elements (camera)) {
    GST_WARNING_OBJECT (camera, "creating view failed");
    goto done;
  }

  /* Link output selector ! view_finder */
  camera->pad_src_view =
      gst_element_get_request_pad (camera->src_out_sel, "src%d");
  camera->pad_view_src =
      gst_element_get_request_pad (camera->view_in_sel, "sink%d");
  link_ret = gst_pad_link (camera->pad_src_view, camera->pad_view_src);
  if (GST_PAD_LINK_FAILED (link_ret)) {
    GST_ELEMENT_ERROR (camera, CORE, NEGOTIATION,
        ("linking view finder failed"), (NULL));
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
  link_ret = gst_pad_link (unconnected_pad, camera->pad_view_vid);
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
    camera->pad_view_vid = NULL;
  }
  if (camera->pad_src_vid) {
    gst_element_release_request_pad (camera->src_out_sel, camera->pad_src_vid);
    camera->pad_src_vid = NULL;
  }
  if (camera->pad_view_img) {
    gst_element_release_request_pad (camera->view_in_sel, camera->pad_view_img);
    camera->pad_view_img = NULL;
  }
  if (camera->pad_src_img) {
    gst_element_release_request_pad (camera->src_out_sel, camera->pad_src_img);
    camera->pad_src_img = NULL;
  }
  if (camera->pad_view_src) {
    gst_element_release_request_pad (camera->view_in_sel, camera->pad_view_src);
    camera->pad_view_src = NULL;
  }
  if (camera->pad_src_view) {
    gst_element_release_request_pad (camera->src_out_sel, camera->pad_src_view);
    camera->pad_src_view = NULL;
  }

  camera->view_sink = NULL;
  camera->view_scale = NULL;
  camera->view_in_sel = NULL;

  camera->src_out_sel = NULL;
  camera->src_filter = NULL;
  camera->src_zoom_crop = NULL;
  camera->src_zoom_scale = NULL;
  camera->src_zoom_filter = NULL;
  camera->src_vid_src = NULL;

  camera->active_bin = NULL;

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
  if (camera->capture_mutex) {
    g_mutex_free (camera->capture_mutex);
    camera->capture_mutex = NULL;
  }
  if (camera->cond) {
    g_cond_free (camera->cond);
    camera->cond = NULL;
  }
  if (camera->filename) {
    g_string_free (camera->filename, TRUE);
    camera->filename = NULL;
  }
  /* Unref user set elements */
  if (camera->user_vf_sink) {
    gst_object_unref (camera->user_vf_sink);
    camera->user_vf_sink = NULL;
  }
  if (camera->user_vid_src) {
    gst_object_unref (camera->user_vid_src);
    camera->user_vid_src = NULL;
  }

  if (camera->image_capture_caps) {
    gst_caps_unref (camera->image_capture_caps);
    camera->image_capture_caps = NULL;
  }

  if (camera->view_finder_caps) {
    gst_caps_unref (camera->view_finder_caps);
    camera->view_finder_caps = NULL;
  }

  if (camera->allowed_caps) {
    gst_caps_unref (camera->allowed_caps);
    camera->allowed_caps = NULL;
  }
}

/*
 * gst_camerabin_image_capture_continue:
 * @camera: camerabin object
 * @filename: new filename set by user
 * @cont: TRUE to continue image capture, FALSE otherwise
 *
 * Check if user wants to continue image capturing by using g_signal.
 */
static void
gst_camerabin_image_capture_continue (GstCameraBin * camera, GString * filename,
    gboolean * cont)
{
  GST_DEBUG_OBJECT (camera, "emitting img_done signal, filename: %s",
      filename->str);
  g_signal_emit (G_OBJECT (camera), camerabin_signals[IMG_DONE_SIGNAL], 0,
      filename, cont);

  GST_DEBUG_OBJECT (camera, "emitted img_done, new filename:%s, continue:%d",
      filename->str, *cont);
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
    GST_DEBUG_OBJECT (camera, "setting mode: %d", mode);
    /* Interrupt ongoing capture */
    gst_camerabin_do_stop (camera);
    camera->mode = mode;
    if (camera->active_bin) {
      gst_element_set_state (camera->active_bin, GST_STATE_NULL);
    }
    if (camera->mode == MODE_IMAGE) {
      camera->active_bin = camera->imgbin;
    } else if (camera->mode == MODE_VIDEO) {
      camera->active_bin = camera->vidbin;
    }
    gst_camerabin_reset_to_view_finder (camera);
  }
}

/*
 * gst_camerabin_change_filename:
 * @camera: camerabin object
 * @name: new filename for capture
 *
 * Change filename for image or video capture.
 * Changing filename will stop ongoing capture.
 */
static void
gst_camerabin_change_filename (GstCameraBin * camera, const gchar * name)
{
  if (0 != strcmp (camera->filename->str, name)) {
    GST_DEBUG_OBJECT (camera, "changing filename from %s to %s",
        camera->filename->str, name);
    /* Interrupt ongoing capture */
    gst_camerabin_do_stop (camera);
    gst_camerabin_reset_to_view_finder (camera);

    if (camera->active_bin) {
      g_object_set (G_OBJECT (camera->active_bin), "filename", name, NULL);
    }

    g_string_assign (camera->filename, name);
  }
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
  gint zoom;
  gboolean done = FALSE;

  g_return_if_fail (camera != NULL);
  g_return_if_fail (camera->src_zoom_crop != NULL);

  zoom = g_atomic_int_get (&camera->zoom);

  g_return_if_fail (zoom);

  if (GST_IS_ELEMENT (camera->src_vid_src) &&
      gst_element_implements_interface (camera->src_vid_src,
          GST_TYPE_PHOTOGRAPHY)) {
    /* Try setting (hardware) zoom using photography interface */
    GstPhotography *photo;
    GstPhotoCaps pcaps;

    photo = GST_PHOTOGRAPHY (camera->src_vid_src);
    pcaps = gst_photography_get_capabilities (photo);

    if (pcaps & GST_PHOTOGRAPHY_CAPS_ZOOM) {
      done = gst_photography_set_zoom (photo, (gfloat) zoom / 100.0);
    }
  }

  if (!done) {
    /* Update capsfilters to apply the (software) zoom */
    gint w2_crop = 0;
    gint h2_crop = 0;
    GstPad *pad_zoom_sink = NULL;

    GST_INFO_OBJECT (camera, "zoom: %d, orig size: %dx%d", zoom,
        camera->width, camera->height);

    if (zoom != ZOOM_1X) {
      w2_crop = (camera->width - (camera->width * ZOOM_1X / zoom)) / 2;
      h2_crop = (camera->height - (camera->height * ZOOM_1X / zoom)) / 2;
    }

    pad_zoom_sink = gst_element_get_static_pad (camera->src_zoom_crop, "sink");

    GST_INFO_OBJECT (camera,
        "sw cropping: left:%d, right:%d, top:%d, bottom:%d", w2_crop, w2_crop,
        h2_crop, h2_crop);

    GST_PAD_STREAM_LOCK (pad_zoom_sink);
    g_object_set (camera->src_zoom_crop, "left", w2_crop, "right", w2_crop,
        "top", h2_crop, "bottom", h2_crop, NULL);

    GST_PAD_STREAM_UNLOCK (pad_zoom_sink);
    gst_object_unref (pad_zoom_sink);
  }
  GST_LOG_OBJECT (camera, "zoom set");
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
  gboolean temp_videosrc_pause = FALSE;
  GstElement *videosrc;

  g_return_val_if_fail (camera != NULL, NULL);

  videosrc = camera->src_vid_src ? camera->src_vid_src : camera->user_vid_src;

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

  /* Make this function work also in READY and NULL state */
  if (state == GST_STATE_READY || state == GST_STATE_NULL) {
    GST_DEBUG_OBJECT (camera, "setting videosrc to paused temporarily");
    temp_videosrc_pause = TRUE;
    peer_pad = gst_pad_get_peer (pad);
    if (peer_pad) {
      gst_pad_unlink (pad, peer_pad);
    }
    /* Set videosrc to PAUSED to open video device */
    gst_element_set_locked_state (videosrc, TRUE);
    gst_element_set_state (videosrc, GST_STATE_PAUSED);
  }

  camera->allowed_caps = gst_pad_get_caps (pad);

  /* Restore state and re-link if necessary */
  if (temp_videosrc_pause) {
    GST_DEBUG_OBJECT (camera, "restoring videosrc state %d", state);
    /* Reset videosrc to NULL state, some drivers seem to need this */
    gst_element_set_state (videosrc, GST_STATE_NULL);
    gst_element_set_state (videosrc, state);
    if (peer_pad) {
      gst_pad_link (pad, peer_pad);
      gst_object_unref (peer_pad);
    }
    gst_element_set_locked_state (videosrc, FALSE);
  }

  gst_object_unref (pad);

done:
  if (camera->allowed_caps) {
    caps = gst_caps_copy (camera->allowed_caps);
  }
failed:
  GST_INFO_OBJECT (camera, "allowed caps:%" GST_PTR_FORMAT, caps);
  return caps;
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
  GstElementFactory *setter_factory;
  const gchar *klass;
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
        setter_factory = gst_element_get_factory (setter);
        klass = gst_element_factory_get_klass (setter_factory);
        /* FIXME: check if tags should be written to all tag setters,
           set tags only to Muxer elements for now */
        if (g_strrstr (klass, "Muxer")) {
          GST_DEBUG ("replacement tags %" GST_PTR_FORMAT, list);
          gst_tag_setter_merge_tags (GST_TAG_SETTER (setter), list,
              GST_TAG_MERGE_REPLACE_ALL);
        }
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
      "image-width", camera->width, "image-height", camera->height, NULL);

  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      "capture-digital-zoom", camera->zoom, 100, NULL);

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

    if (!strcasecmp (channel->label, "brightness")) {
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
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          "capture-brightness", cur_value, 1, NULL);
    } else if (!strcasecmp (channel->label, "contrast")) {
      /* 0 = Normal, 1 = Soft, 2 = Hard */

      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          "capture-contrast",
          (cur_value == mid_value) ? 0 : ((cur_value < mid_value) ? 1 : 2),
          NULL);
    } else if (!strcasecmp (channel->label, "gain")) {
      /* 0 = Normal, 1 = Low Up, 2 = High Up, 3 = Low Down, 4 = Hight Down */
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          "capture-gain",
          (guint) (cur_value == mid_value) ? 0 : ((cur_value <
                  mid_value) ? 1 : 3), NULL);
    } else if (!strcasecmp (channel->label, "saturation")) {
      /* 0 = Normal, 1 = Low, 2 = High */
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          "capture-saturation",
          (cur_value == mid_value) ? 0 : ((cur_value < mid_value) ? 1 : 2),
          NULL);
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
  gst_camerabin_rewrite_tags_to_bin (GST_BIN (camera->active_bin), list);

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
  GstStructure *st;

  GST_INFO_OBJECT (camera, "new_caps:%" GST_PTR_FORMAT, new_caps);

  st = gst_caps_get_structure (new_caps, 0);

  gst_structure_get_int (st, "width", &camera->width);
  gst_structure_get_int (st, "height", &camera->height);

  if (gst_structure_has_field (st, "framerate")) {
    gst_structure_get_fraction (st, "framerate", &camera->fps_n,
        &camera->fps_d);
  }

  /* Update zoom */
  gst_camerabin_setup_zoom (camera);

  /* Update capsfilters */
  g_object_set (G_OBJECT (camera->src_filter), "caps", new_caps, NULL);
  g_object_set (G_OBJECT (camera->src_zoom_filter), "caps", new_caps, NULL);
}

/*
 * img_capture_prepared:
 * @data: camerabin object
 *
 * Callback which is called after image capture has been prepared.
 */
static void
img_capture_prepared (gpointer data)
{
  GstCameraBin *camera = GST_CAMERABIN (data);

  GST_INFO_OBJECT (camera, "image capture prepared");

  if (camera->image_capture_caps) {
    /* Set capsfilters to match arriving image data */
    gst_camerabin_set_capsfilter_caps (camera, camera->image_capture_caps);
  }

  g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
      "active-pad", camera->pad_src_img, NULL);
  gst_camerabin_rewrite_tags (camera);
  gst_element_set_state (GST_ELEMENT (camera->imgbin), GST_STATE_PLAYING);
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
  GstStateChangeReturn state_ret;
  gboolean wait_for_prepare = FALSE;
  gint width = 0, height = 0, fps_n = 0, fps_d = 0;
  GstStructure *st;

  GST_INFO_OBJECT (camera, "starting image capture");

  if (GST_IS_ELEMENT (camera->src_vid_src) &&
      gst_element_implements_interface (camera->src_vid_src,
          GST_TYPE_PHOTOGRAPHY)) {
    /* Start image capture preparations using photography iface */
    wait_for_prepare = TRUE;
    g_mutex_lock (camera->capture_mutex);
    if (camera->image_capture_caps) {
      st = gst_caps_get_structure (camera->image_capture_caps, 0);
    } else {
      st = gst_caps_get_structure (camera->view_finder_caps, 0);
    }
    gst_structure_get_int (st, "width", &width);
    gst_structure_get_int (st, "height", &height);
    gst_structure_get_fraction (st, "framerate", &fps_n, &fps_d);
    /* Set image capture resolution and frame rate */
    g_signal_emit_by_name (camera->src_vid_src, "user-res-fps",
        width, height, fps_n, fps_d, 0);

    /* Enable still image capture mode in v4l2camsrc */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (camera->src_vid_src),
            "capture-mode")) {
      g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 1, NULL);
    }

    /* Start preparations for image capture */
    gst_photography_prepare_for_capture (GST_PHOTOGRAPHY (camera->src_vid_src),
        (GstPhotoCapturePrepared) img_capture_prepared, camera);
    camera->capturing = TRUE;
    g_mutex_unlock (camera->capture_mutex);
  }

  if (!wait_for_prepare) {
    gst_camerabin_rewrite_tags (camera);
    state_ret = gst_element_set_state (camera->imgbin, GST_STATE_PLAYING);
    if (state_ret != GST_STATE_CHANGE_FAILURE) {
      g_mutex_lock (camera->capture_mutex);
      g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", TRUE,
          "active-pad", camera->pad_src_img, NULL);
      camera->capturing = TRUE;
      g_mutex_unlock (camera->capture_mutex);
    } else {
      GST_WARNING_OBJECT (camera, "imagebin state change failed");
      gst_element_set_state (camera->imgbin, GST_STATE_NULL);
    }
  }
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
  /* FIXME: how to ensure resolution and fps is supported by CPU?
   * use a queue overrun signal?
   */
  GST_INFO_OBJECT (camera, "starting video capture");

  gst_camerabin_rewrite_tags (camera);

  /* Pause the pipeline in order to distribute new clock in paused_to_playing */
  /* audio src timestamps will be 0 without state change to READY. ??? */
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_READY);
  gst_element_set_locked_state (camera->vidbin, FALSE);
  state_ret = gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PAUSED);

  if (state_ret != GST_STATE_CHANGE_FAILURE) {
    g_mutex_lock (camera->capture_mutex);
    g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
        "active-pad", camera->pad_src_vid, NULL);

    /* Enable video mode in v4l2camsrc */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (camera->src_vid_src),
            "capture-mode")) {
      g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 2, NULL);
    }

    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING);
    gst_element_set_locked_state (camera->vidbin, TRUE);
    camera->capturing = TRUE;
    g_mutex_unlock (camera->capture_mutex);
  } else {
    GST_WARNING_OBJECT (camera, "videobin state change failed");
    gst_element_set_state (camera->vidbin, GST_STATE_NULL);
    gst_camerabin_reset_to_view_finder (camera);
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

  /* Send eos event to video bin */
  GST_INFO_OBJECT (camera, "sending eos to videobin");
  videopad = gst_element_get_static_pad (camera->vidbin, "sink");
  gst_pad_send_event (videopad, gst_event_new_eos ());
  gst_object_unref (videopad);
}

/*
 * image_pad_blocked:
 * @pad: pad to block/unblock
 * @blocked: TRUE to block, FALSE to unblock
 * @u_data: camera bin object
 *
 * Sends eos event to image bin if blocking pad leading to image bin.
 * The pad will be unblocked when image bin posts eos message.
 */
static void
image_pad_blocked (GstPad * pad, gboolean blocked, gpointer user_data)
{
  GstCameraBin *camera;

  camera = (GstCameraBin *) user_data;

  GST_DEBUG_OBJECT (camera, "%s %s:%s",
      blocked ? "blocking" : "unblocking", GST_DEBUG_PAD_NAME (pad));

  if (blocked && (pad == camera->pad_src_img)) {
    /* Send eos and block until image bin reaches eos */
    GST_DEBUG_OBJECT (camera, "sending eos to image bin");
    gst_element_send_event (camera->imgbin, gst_event_new_eos ());
  }
}

/*
 * gst_camerabin_have_img_buffer:
 * @pad: output-selector src pad leading to image bin
 * @buffer: still image frame
 * @u_data: camera bin object
 *
 * Buffer probe called before sending each buffer to image bin.
 *
 * First buffer is always passed directly to image bin. Then pad
 * is blocked in order to interleave buffers with eos events.
 * Interleaving eos events and buffers is needed when we have
 * decoupled elements in the image bin capture pipeline.
 * After image bin posts eos message, then pad is unblocked.
 * Next, image bin is changed to READY state in order to save the
 * file and the application is allowed to decide whether to
 * continue image capture. If yes, only then the next buffer is
 * passed to image bin.
 */
static gboolean
gst_camerabin_have_img_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data)
{
  GstCameraBin *camera = (GstCameraBin *) u_data;
  gboolean ret = TRUE;

  GST_LOG ("got buffer #%d %p with size %d", camera->num_img_buffers,
      buffer, GST_BUFFER_SIZE (buffer));

  /* Image filename should be set by now */
  if (g_str_equal (camera->filename->str, "")) {
    GST_DEBUG_OBJECT (camera, "filename not set, dropping buffer");
    ret = FALSE;
    goto done;
  }

  /* Check for first buffer after capture start, we want to
     pass it forward directly. */
  if (!camera->num_img_buffers) {
    /* Restore filter caps for view finder mode if necessary.
       The v4l2camsrc switches automatically to view finder
       resolution after hi-res still image capture. */
    if (camera->image_capture_caps) {
      gst_camerabin_set_capsfilter_caps (camera, camera->view_finder_caps);
    }
    goto done;
  }

  /* Close the file of saved image */
  gst_element_set_state (camera->imgbin, GST_STATE_READY);

  /* Check if the application wants to continue */
  gst_camerabin_image_capture_continue (camera, camera->filename, &ret);

  if (ret && !camera->stop_requested) {
    GST_DEBUG_OBJECT (camera, "capturing image \"%s\"", camera->filename->str);
    g_object_set (G_OBJECT (camera->imgbin), "filename",
        camera->filename->str, NULL);
    gst_element_set_state (camera->imgbin, GST_STATE_PLAYING);
  } else {
    GST_DEBUG_OBJECT (camera, "not continuing (cont:%d, stop_req:%d)",
        ret, camera->stop_requested);
    /* Reset filename to force application set new filename */
    g_string_assign (camera->filename, "");

    /* Block dataflow to the output-selector to show preview image in
       view finder. Continue and unblock when capture is stopped */
    gst_pad_set_blocked_async (camera->srcpad_zoom_filter, TRUE,
        (GstPadBlockCallback) image_pad_blocked, camera);
    ret = FALSE;                /* Drop the buffer */

    g_mutex_lock (camera->capture_mutex);
    camera->capturing = FALSE;
    g_cond_signal (camera->cond);
    g_mutex_unlock (camera->capture_mutex);
  }

done:

  if (ret) {
    camera->num_img_buffers++;
    /* Block when next buffer arrives, we want to push eos event
       between frames and make sure that eos reaches the filesink
       before processing the next buffer. */
    gst_pad_set_blocked_async (pad, TRUE,
        (GstPadBlockCallback) image_pad_blocked, camera);
  }

  return ret;
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
  if (camera->stop_requested) {
    gst_camerabin_send_video_eos (camera);
    ret = FALSE;                /* Drop buffer */
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

  /* Set active bin to READY state */
  if (camera->active_bin) {
    state_ret = gst_element_set_state (camera->active_bin, GST_STATE_READY);
    if (state_ret == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (camera, "state change failed");
      gst_element_set_state (camera->active_bin, GST_STATE_NULL);
      camera->active_bin = NULL;
    }
  }

  /* Reset counters and flags */
  camera->num_img_buffers = 0;
  camera->stop_requested = FALSE;
  camera->paused = FALSE;

  if (camera->src_out_sel) {
    /* Set selector to forward data to view finder */
    g_object_set (G_OBJECT (camera->src_out_sel), "resend-latest", FALSE,
        "active-pad", camera->pad_src_view, NULL);
  }

  /* Unblock, if dataflow to output-selector is blocked due to image preview */
  if (camera->srcpad_zoom_filter &&
      gst_pad_is_blocked (camera->srcpad_zoom_filter)) {
    gst_pad_set_blocked_async (camera->srcpad_zoom_filter, FALSE,
        (GstPadBlockCallback) image_pad_blocked, camera);
  }
  /* Unblock, if dataflow to image bin is blocked due to waiting for eos */
  if (camera->pad_src_img && gst_pad_is_blocked (camera->pad_src_img)) {
    gst_pad_set_blocked_async (camera->pad_src_img, FALSE,
        (GstPadBlockCallback) image_pad_blocked, camera);
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
  g_mutex_lock (camera->capture_mutex);
  if (camera->capturing) {
    GST_DEBUG_OBJECT (camera, "mark stop");
    camera->stop_requested = TRUE;

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
  }
  g_mutex_unlock (camera->capture_mutex);
}

/*
 * gst_camerabin_default_signal_img_done:
 * @camera: camerabin object
 * @fname: new filename
 *
 * Default handler for #GstCameraBin::img-done signal,
 * stops always capture.
 *
 * Returns: FALSE always
 */
static gboolean
gst_camerabin_default_signal_img_done (GstCameraBin * camera, GString * fname)
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
  GstCaps *allowed_caps = NULL, *intersect = NULL;
  const GValue *framerate = NULL;
  guint caps_size, i;

  /* Get supported caps from video src that matches with new filter caps */
  GST_INFO_OBJECT (camera, "filter caps:%" GST_PTR_FORMAT, filter_caps);
  allowed_caps = gst_camerabin_get_allowed_input_caps (camera);
  intersect = gst_caps_intersect (allowed_caps, filter_caps);
  GST_INFO_OBJECT (camera, "intersect caps:%" GST_PTR_FORMAT, intersect);

  /* Find the best framerate from the caps */
  caps_size = gst_caps_get_size (intersect);
  for (i = 0; i < caps_size; i++) {
    structure = gst_caps_get_structure (intersect, i);
    framerate =
        gst_camerabin_find_better_framerate (camera, structure, framerate);
  }

  if (GST_VALUE_HOLDS_FRACTION (framerate)) {
    gst_caps_set_simple (filter_caps,
        "framerate", GST_TYPE_FRACTION,
        gst_value_get_fraction_numerator (framerate),
        gst_value_get_fraction_denominator (framerate), NULL);
  }

  if (allowed_caps) {
    gst_caps_unref (allowed_caps);
  }
  if (intersect) {
    gst_caps_unref (intersect);
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
    GST_LOG_OBJECT (camera, "finding min framerate");
    comparison = GST_VALUE_LESS_THAN;
  } else {
    GST_LOG_OBJECT (camera, "finding max framerate");
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
 * GObject callback functions implementation
 */

static void
gst_camerabin_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "Camera Bin",
    "Generic/Bin/Camera",
    "Handle lot of features present in DSC",
    "Nokia Corporation <multimedia@maemo.org>\n"
        "Edgard Lima <edgard.lima@indt.org.br>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details (element_class, &element_details);
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
          "Filename of the image or video to save", "", G_PARAM_READWRITE));

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
          GST_TYPE_CAMERABIN_MODE, DEFAULT_MODE, G_PARAM_READWRITE));

  /**
   * GstCameraBin:mute:
   *
   * Mute audio in video recording mode.
   * Set this property only when #GstCameraBin is in READY, PAUSED or PLAYING.
   */

  g_object_class_install_property (gobject_class, ARG_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "True to mute the recording. False to record with audio",
          ARG_DEFAULT_MUTE, G_PARAM_READWRITE));

  /**
   * GstCameraBin:zoom:
   *
   * Set up the zoom applied to the frames.
   * Set this property only when #GstCameraBin is in READY, PAUSED or PLAYING.
   */

  g_object_class_install_property (gobject_class, ARG_ZOOM,
      g_param_spec_int ("zoom", "Zoom",
          "The zoom. 100 for 1x, 200 for 2x and so on",
          MIN_ZOOM, MAX_ZOOM, DEFAULT_ZOOM, G_PARAM_READWRITE));

  /**
   * GstCameraBin:imagepp:
   *
   * Set up an element to do image post processing.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */
  g_object_class_install_property (gobject_class, ARG_IMAGE_POST,
      g_param_spec_object ("imagepp", "Image post processing element",
          "Image Post-Processing GStreamer element (default is NULL)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:imageenc:
   *
   * Set up an image encoder (for example, jpegenc or pngenc) element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_IMAGE_ENC,
      g_param_spec_object ("imageenc", "Image encoder",
          "Image encoder GStreamer element (default is jpegenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:videopp:
   *
   * Set up an element to do video post processing.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_POST,
      g_param_spec_object ("videopp", "Video post processing element",
          "Video post processing GStreamer element (default is NULL)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:videoenc:
   *
   * Set up a video encoder element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_ENC,
      g_param_spec_object ("videoenc", "Video encoder",
          "Video encoder GStreamer element (default is theoraenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:audioenc:
   *
   * Set up an audio encoder element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_AUDIO_ENC,
      g_param_spec_object ("audioenc", "Audio encoder",
          "Audio encoder GStreamer element (default is vorbisenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:videomux:
   *
   * Set up a video muxer element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_MUX,
      g_param_spec_object ("videomux", "Video muxer",
          "Video muxer GStreamer element (default is oggmux)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:vfsink:
   *
   * Set up a sink element to render frames in view finder.
   * By default "autovideosink" will be the sink element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VF_SINK,
      g_param_spec_object ("vfsink", "View finder sink",
          "View finder sink GStreamer element (default is autovideosink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   *  GstCameraBin:videosrc:
   *
   * Set up a video source element.
   * By default "v4l2camsrc" will be the src element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_VIDEO_SRC,
      g_param_spec_object ("videosrc", "Video source element",
          "Video source GStreamer element (default is v4l2camsrc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  /**
   *  GstCameraBin:audiosrc:
   *
   * Set up an audio source element.
   * By default "pulsesrc" will be the source element.
   * This property can only be set while #GstCameraBin is in NULL state.
   * The ownership of the element will be taken by #GstCameraBin.
   */

  g_object_class_install_property (gobject_class, ARG_AUDIO_SRC,
      g_param_spec_object ("audiosrc", "Audio source element",
          "Audio source GStreamer element (default is pulsesrc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));

  /**
   * GstCameraBin:inputcaps:
   *
   * The allowed modes of operation of the video source. Have in mind that it
   * doesn't mean #GstCameraBin can operate in all those modes,
   * it depends also on the other elements in the pipeline. Remember to
   * gst_caps_unref after using it.
   */

  g_object_class_install_property (gobject_class, ARG_INPUT_CAPS,
      g_param_spec_boxed ("inputcaps", "Input caps",
          "The allowed modes of the video source operation",
          GST_TYPE_CAPS, G_PARAM_READABLE));

  /**
   * GstCameraBin:filter-caps:
   *
   * Filter video source element caps using this property.
   * This is an alternative to #GstCamerabin::user-res-fps action
   * signal that allows more fine grained control of video source.
   */

  g_object_class_install_property (gobject_class, ARG_FILTER_CAPS,
      g_param_spec_boxed ("filter-caps", "Filter caps",
          "Capsfilter caps used to control video source operation",
          GST_TYPE_CAPS, G_PARAM_READWRITE));

  /**
   * GstCameraBin::user-start:
   * @camera: the camera bin element
   *
   * Starts image capture or video recording depending on the Mode.
   * If there is a capture already going on, does nothing.
   * Resumes video recording if it has been paused.
   */

  camerabin_signals[USER_START_SIGNAL] =
      g_signal_new ("user-start",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, user_start),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::user-stop:
   * @camera: the camera bin element
   *
   * Stops still image preview, continuous image capture and video
   * recording and returns to the view finder mode.
   */

  camerabin_signals[USER_STOP_SIGNAL] =
      g_signal_new ("user-stop",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, user_stop),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::user-pause:
   * @camera: the camera bin element
   *
   * Pauses video recording or resumes paused video recording.
   * If in image mode or not recording, does nothing.
   */

  camerabin_signals[USER_PAUSE_SIGNAL] =
      g_signal_new ("user-pause",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, user_pause),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstCameraBin::user-res-fps:
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
   */

  camerabin_signals[USER_RES_FPS_SIGNAL] =
      g_signal_new ("user-res-fps",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, user_res_fps),
      NULL, NULL, gst_camerabin_marshal_VOID__INT_INT_INT_INT, G_TYPE_NONE, 4,
      G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

  /**
   * GstCameraBin::user-image-res:
   * @camera: the camera bin element
   * @width: number of horizontal pixels
   * @height: number of vertical pixels
   *
   * Changes the resolution used for still image capture.
   * Does not affect view finder mode and video recording.
   * Use this action signal in PAUSED or PLAYING state.
   */

  camerabin_signals[USER_IMAGE_RES_SIGNAL] =
      g_signal_new ("user-image-res",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstCameraBinClass, user_image_res),
      NULL, NULL, gst_camerabin_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
      G_TYPE_INT, G_TYPE_INT);

  /**
   * GstCameraBin::img-done:
   * @camera: the camera bin element
   * @filename: the name of the file just saved
   *
   * Signal emited when the file has just been saved. To continue taking
   * pictures just update @filename and return TRUE, otherwise return FALSE.
   *
   * Don't call any #GstCameraBin method from this signal, if you do so there
   * will be a deadlock.
   */

  camerabin_signals[IMG_DONE_SIGNAL] =
      g_signal_new ("img-done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstCameraBinClass, img_done),
      g_signal_accumulator_true_handled, NULL, gst_marshal_BOOLEAN__POINTER,
      G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);

  klass->user_start = gst_camerabin_user_start;
  klass->user_stop = gst_camerabin_user_stop;
  klass->user_pause = gst_camerabin_user_pause;
  klass->user_res_fps = gst_camerabin_user_res_fps;
  klass->user_image_res = gst_camerabin_user_image_res;

  klass->img_done = gst_camerabin_default_signal_img_done;

  /* gstelement */

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_camerabin_change_state);

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
  camera->num_img_buffers = 0;
  camera->stop_requested = FALSE;
  camera->paused = FALSE;
  camera->capturing = FALSE;
  camera->night_mode = FALSE;

  camera->width = DEFAULT_WIDTH;
  camera->height = DEFAULT_HEIGHT;
  camera->fps_n = DEFAULT_FPS_N;
  camera->fps_d = DEFAULT_FPS_D;

  camera->image_capture_caps = NULL;
  camera->view_finder_caps = NULL;
  camera->allowed_caps = NULL;

  camera->zoom = DEFAULT_ZOOM;

  /* concurrency control */
  camera->capture_mutex = g_mutex_new ();
  camera->cond = g_cond_new ();

  /* pad names for output and input selectors */
  camera->pad_src_view = NULL;
  camera->pad_view_src = NULL;
  camera->pad_src_img = NULL;
  camera->pad_view_img = NULL;
  camera->pad_src_vid = NULL;
  camera->pad_view_vid = NULL;
  camera->srcpad_zoom_filter = NULL;

  /* source elements */
  camera->src_vid_src = NULL;
  camera->src_filter = NULL;
  camera->src_zoom_crop = NULL;
  camera->src_zoom_scale = NULL;
  camera->src_zoom_filter = NULL;
  camera->src_out_sel = NULL;

  camera->user_vf_sink = NULL;

  /* image capture bin */
  camera->imgbin = g_object_new (GST_TYPE_CAMERABIN_IMAGE, NULL);
  gst_object_ref (camera->imgbin);

  /* video capture bin */
  camera->vidbin = g_object_new (GST_TYPE_CAMERABIN_VIDEO, NULL);
  gst_object_ref (camera->vidbin);

  camera->active_bin = NULL;

  /* view finder elements */
  camera->view_in_sel = NULL;
  camera->view_scale = NULL;
  camera->view_sink = NULL;
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
      g_atomic_int_set (&camera->zoom, g_value_get_int (value));
      gst_camerabin_setup_zoom (camera);
      break;
    case ARG_MODE:
      gst_camerabin_change_mode (camera, g_value_get_enum (value));
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
        GST_WARNING_OBJECT (camera->vidbin,
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
    case ARG_VF_SINK:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the view finder element"),
            (NULL));
      } else {
        if (camera->user_vf_sink)
          gst_object_unref (camera->user_vf_sink);
        camera->user_vf_sink = g_value_get_object (value);
        gst_object_ref (camera->user_vf_sink);
      }
      break;
    case ARG_VIDEO_SRC:
      if (GST_STATE (camera) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (camera, CORE, FAILED,
            ("camerabin must be in NULL state when setting the video source element"),
            (NULL));
      } else {
        if (camera->user_vid_src)
          gst_object_unref (camera->user_vid_src);
        camera->user_vid_src = g_value_get_object (value);
        gst_object_ref (camera->user_vid_src);
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
    case ARG_FILTER_CAPS:
      GST_OBJECT_LOCK (camera);
      if (camera->view_finder_caps) {
        gst_caps_unref (camera->view_finder_caps);
      }
      camera->view_finder_caps = gst_caps_copy (gst_value_get_caps (value));
      GST_OBJECT_UNLOCK (camera);
      gst_camerabin_set_capsfilter_caps (camera, camera->view_finder_caps);
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
    case ARG_MUTE:
      g_value_set_boolean (value,
          gst_camerabin_video_get_mute (GST_CAMERABIN_VIDEO (camera->vidbin)));
      break;
    case ARG_ZOOM:
      g_value_set_int (value, g_atomic_int_get (&camera->zoom));
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
      g_value_set_object (value, camera->user_vf_sink);
      break;
    case ARG_VIDEO_SRC:
      g_value_set_object (value, camera->src_vid_src);
      break;
    case ARG_AUDIO_SRC:
      g_value_set_object (value,
          gst_camerabin_video_get_audio_src (GST_CAMERABIN_VIDEO
              (camera->vidbin)));
      break;
    case ARG_INPUT_CAPS:
      gst_value_set_caps (value, gst_camerabin_get_allowed_input_caps (camera));
      break;
    case ARG_FILTER_CAPS:
      gst_value_set_caps (value, camera->view_finder_caps);
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
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_element_set_locked_state (camera->imgbin, FALSE);
      gst_element_set_locked_state (camera->vidbin, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (camera, "PAUSED to READY");
      g_mutex_lock (camera->capture_mutex);
      if (camera->capturing) {
        GST_WARNING_OBJECT (camera, "was capturing when changing to READY");
        camera->capturing = FALSE;
        /* Reset capture and don't wait for capturing to finish properly.
           Proper capturing should have been finished before going to READY. */
        gst_camerabin_reset_to_view_finder (camera);
        g_cond_signal (camera->cond);
      }
      g_mutex_unlock (camera->capture_mutex);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      camerabin_destroy_elements (camera);
      break;
    default:
      break;
  }

done:

  return ret;
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
        g_mutex_unlock (camera->capture_mutex);
      } else if (GST_MESSAGE_SRC (msg) == GST_OBJECT (camera->imgbin)) {
        /* Image eos */
        GST_DEBUG_OBJECT (camera, "got image eos message");
        /* Unblock pad to process next buffer */
        gst_pad_set_blocked_async (camera->pad_src_img, FALSE,
            (GstPadBlockCallback) image_pad_blocked, camera);
      }
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
gst_camerabin_user_start (GstCameraBin * camera)
{

  GST_INFO_OBJECT (camera, "starting capture");
  if (camera->paused) {
    gst_camerabin_user_pause (camera);
    return;
  }

  if (!camera->active_bin) {
    GST_INFO_OBJECT (camera, "mode not explicitly set by application");
    gst_camerabin_change_mode (camera, camera->mode);
  }

  if (g_str_equal (camera->filename->str, "")) {
    GST_ELEMENT_ERROR (camera, CORE, FAILED,
        ("set filename before starting capture"), (NULL));
    return;
  }

  g_mutex_lock (camera->capture_mutex);
  if (camera->capturing) {
    GST_WARNING_OBJECT (camera, "capturing \"%s\" ongoing, set new filename",
        camera->filename->str);
    g_mutex_unlock (camera->capture_mutex);
    return;
  }
  g_mutex_unlock (camera->capture_mutex);

  g_object_set (G_OBJECT (camera->active_bin), "filename",
      camera->filename->str, NULL);

  if (camera->active_bin == camera->imgbin) {
    gst_camerabin_start_image_capture (camera);
  } else if (camera->active_bin == camera->vidbin) {
    gst_camerabin_start_video_recording (camera);
  }
}

static void
gst_camerabin_user_stop (GstCameraBin * camera)
{
  GST_INFO_OBJECT (camera, "stopping %s capture",
      camera->mode ? "video" : "image");
  gst_camerabin_do_stop (camera);
  gst_camerabin_reset_to_view_finder (camera);
}

static void
gst_camerabin_user_pause (GstCameraBin * camera)
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

      /* Enable view finder mode in v4l2camsrc */
      if (g_object_class_find_property (G_OBJECT_GET_CLASS
              (camera->src_vid_src), "capture-mode")) {
        g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 0, NULL);
      }

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

      /* Enable video recording mode in v4l2camsrc */
      if (g_object_class_find_property (G_OBJECT_GET_CLASS
              (camera->src_vid_src), "capture-mode")) {
        g_object_set (G_OBJECT (camera->src_vid_src), "capture-mode", 2, NULL);
      }

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

static void
gst_camerabin_user_res_fps (GstCameraBin * camera, gint width, gint height,
    gint fps_n, gint fps_d)
{
  GstState state;

  GST_INFO_OBJECT (camera, "switching resolution to %dx%d and fps to %d/%d",
      width, height, fps_n, fps_d);

  state = GST_STATE (camera);
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_READY);
  camera->width = width;
  camera->height = height;
  camera->fps_n = fps_n;
  camera->fps_d = fps_d;
  gst_element_set_state (GST_ELEMENT (camera), state);
}

static void
gst_camerabin_user_image_res (GstCameraBin * camera, gint width, gint height)
{
  GstStructure *structure;
  GstCaps *new_caps = NULL;
  guint32 format = 0;

  g_return_if_fail (camera != NULL);

  if (width && height && camera->view_finder_caps) {
    /* Use view finder mode caps as a basis */
    structure = gst_caps_get_structure (camera->view_finder_caps, 0);

    /* Set new resolution for image capture */
    new_caps = gst_caps_new_simple (gst_structure_get_name (structure),
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

    /* Set format according to current videosrc format */
    format = get_srcpad_current_format (camera->src_vid_src);
    if (format) {
      gst_caps_set_simple (new_caps, "format", GST_TYPE_FOURCC, format, NULL);
    }

    /* Set allowed framerate for the resolution. */
    gst_camerabin_set_allowed_framerate (camera, new_caps);

    /* Reset the format to match with view finder mode caps */
    if (gst_structure_get_fourcc (structure, "format", &format)) {
      gst_caps_set_simple (new_caps, "format", GST_TYPE_FOURCC, format, NULL);
    }
  }

  GST_INFO_OBJECT (camera,
      "init filter caps for image capture %" GST_PTR_FORMAT, new_caps);
  gst_caps_replace (&camera->image_capture_caps, new_caps);
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
    plugin_init, VERSION, "LGPL", "Nokia", "http://www.nokia.com/")
