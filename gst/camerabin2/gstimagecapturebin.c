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
 * SECTION:element-gstimagecapturebin
 *
 * The gstimagecapturebin element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=3 ! imagecapturebin
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstimagecapturebin.h"
#include "camerabingeneral.h"
#include <gst/pbutils/pbutils.h>
#include <gst/gst-i18n-plugin.h>

/* prototypes */


enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_ENCODER,
  PROP_MUXER
};

#define DEFAULT_LOCATION "img_%d"
#define DEFAULT_COLORSPACE "ffmpegcolorspace"
#define DEFAULT_ENCODER "jpegenc"
#define DEFAULT_MUXER "jifmux"
#define DEFAULT_SINK "multifilesink"

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

/* class initialization */

GST_BOILERPLATE (GstImageCaptureBin, gst_image_capture_bin, GstBin,
    GST_TYPE_BIN);

/* GObject callbacks */
static void gst_image_capture_bin_dispose (GObject * object);
static void gst_image_capture_bin_finalize (GObject * object);

/* Element class functions */
static GstStateChangeReturn
gst_image_capture_bin_change_state (GstElement * element, GstStateChange trans);

static void
gst_image_capture_bin_set_encoder (GstImageCaptureBin * imagebin,
    GstElement * encoder)
{
  GST_DEBUG_OBJECT (GST_OBJECT (imagebin),
      "Setting image encoder %" GST_PTR_FORMAT, encoder);

  if (imagebin->user_encoder)
    g_object_unref (imagebin->user_encoder);

  if (encoder)
    g_object_ref (encoder);

  imagebin->user_encoder = encoder;
}

static void
gst_image_capture_bin_set_muxer (GstImageCaptureBin * imagebin,
    GstElement * muxer)
{
  GST_DEBUG_OBJECT (GST_OBJECT (imagebin),
      "Setting image muxer %" GST_PTR_FORMAT, muxer);

  if (imagebin->user_muxer)
    g_object_unref (imagebin->user_muxer);

  if (muxer)
    g_object_ref (muxer);

  imagebin->user_muxer = muxer;
}

static void
gst_image_capture_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImageCaptureBin *imagebin = GST_IMAGE_CAPTURE_BIN_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_free (imagebin->location);
      imagebin->location = g_value_dup_string (value);
      GST_DEBUG_OBJECT (imagebin, "setting location to %s", imagebin->location);
      if (imagebin->sink) {
        g_object_set (imagebin->sink, "location", imagebin->location, NULL);
      }
      break;
    case PROP_ENCODER:
      gst_image_capture_bin_set_encoder (imagebin, g_value_get_object (value));
      break;
    case PROP_MUXER:
      gst_image_capture_bin_set_muxer (imagebin, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_image_capture_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImageCaptureBin *imagebin = GST_IMAGE_CAPTURE_BIN_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, imagebin->location);
      break;
    case PROP_ENCODER:
      g_value_set_object (value, imagebin->encoder);
      break;
    case PROP_MUXER:
      g_value_set_object (value, imagebin->muxer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_image_capture_bin_finalize (GObject * object)
{
  GstImageCaptureBin *imgbin = GST_IMAGE_CAPTURE_BIN_CAST (object);

  g_free (imgbin->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_image_capture_bin_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class, "Image Capture Bin",
      "Sink/Video", "Image Capture Bin used in camerabin2",
      "Thiago Santos <thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_image_capture_bin_class_init (GstImageCaptureBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = gst_image_capture_bin_dispose;
  gobject_class->finalize = gst_image_capture_bin_finalize;
  gobject_class->set_property = gst_image_capture_bin_set_property;
  gobject_class->get_property = gst_image_capture_bin_get_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_image_capture_bin_change_state);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to save the captured files. A %%d can be used as a "
          "placeholder for a capture count",
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENCODER,
      g_param_spec_object ("image-encoder", "Image encoder",
          "Image encoder GStreamer element (default is jpegenc)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MUXER,
      g_param_spec_object ("image-muxer", "Image muxer",
          "Image muxer GStreamer element (default is jifmux)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_image_capture_bin_init (GstImageCaptureBin * imagebin,
    GstImageCaptureBinClass * imagebin_class)
{
  GstPadTemplate *tmpl;

  tmpl = gst_static_pad_template_get (&sink_template);
  imagebin->ghostpad = gst_ghost_pad_new_no_target_from_template ("sink", tmpl);
  gst_object_unref (tmpl);
  gst_element_add_pad (GST_ELEMENT_CAST (imagebin), imagebin->ghostpad);

  imagebin->sink = NULL;

  imagebin->location = g_strdup (DEFAULT_LOCATION);
  imagebin->encoder = NULL;
  imagebin->user_encoder = NULL;
  imagebin->muxer = NULL;
  imagebin->user_muxer = NULL;
}

static void
gst_image_capture_bin_dispose (GObject * object)
{
  GstImageCaptureBin *imagebin = GST_IMAGE_CAPTURE_BIN_CAST (object);

  if (imagebin->user_encoder) {
    gst_object_unref (imagebin->user_encoder);
    imagebin->user_encoder = NULL;
  }

  if (imagebin->user_muxer) {
    gst_object_unref (imagebin->user_muxer);
    imagebin->user_muxer = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) imagebin);
}

static gboolean
gst_image_capture_bin_create_elements (GstImageCaptureBin * imagebin)
{
  GstElement *colorspace;
  GstPad *pad = NULL;
  const gchar *missing_element_name;

  if (imagebin->elements_created)
    return TRUE;

  /* create elements */
  colorspace =
      gst_camerabin_create_and_add_element (GST_BIN (imagebin),
      DEFAULT_COLORSPACE, "imagebin-colorspace");
  if (!colorspace) {
    missing_element_name = DEFAULT_COLORSPACE;
    goto missing_element;
  }

  if (imagebin->user_encoder) {
    imagebin->encoder = imagebin->user_encoder;
    if (!gst_camerabin_add_element (GST_BIN (imagebin), imagebin->encoder)) {
      goto error;
    }
  } else {
    imagebin->encoder =
        gst_camerabin_create_and_add_element (GST_BIN (imagebin),
        DEFAULT_ENCODER, "imagebin-encoder");
    if (!imagebin->encoder) {
      missing_element_name = DEFAULT_ENCODER;
      goto missing_element;
    }
  }

  if (imagebin->user_muxer) {
    imagebin->muxer = imagebin->user_muxer;
    if (!gst_camerabin_add_element (GST_BIN (imagebin), imagebin->muxer)) {
      goto error;
    }
  } else {
    imagebin->muxer =
        gst_camerabin_create_and_add_element (GST_BIN (imagebin),
        DEFAULT_MUXER, "imagebin-muxer");
    if (!imagebin->muxer) {
      missing_element_name = DEFAULT_MUXER;
      goto missing_element;
    }
  }

  imagebin->sink =
      gst_camerabin_create_and_add_element (GST_BIN (imagebin), DEFAULT_SINK,
      "imagebin-sink");
  if (!imagebin->sink) {
    missing_element_name = DEFAULT_SINK;
    goto missing_element;
  }

  g_object_set (imagebin->sink, "location", imagebin->location, "async", FALSE,
      "post-messages", TRUE, NULL);

  /* add ghostpad */
  pad = gst_element_get_static_pad (colorspace, "sink");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (imagebin->ghostpad), pad))
    goto error;
  gst_object_unref (pad);

  imagebin->elements_created = TRUE;
  return TRUE;

missing_element:
  gst_element_post_message (GST_ELEMENT_CAST (imagebin),
      gst_missing_element_message_new (GST_ELEMENT_CAST (imagebin),
          missing_element_name));
  GST_ELEMENT_ERROR (imagebin, CORE, MISSING_PLUGIN,
      (_("Missing element '%s' - check your GStreamer installation."),
          missing_element_name), (NULL));
  goto error;

error:
  if (pad)
    gst_object_unref (pad);
  return FALSE;
}

static GstStateChangeReturn
gst_image_capture_bin_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstImageCaptureBin *imagebin = GST_IMAGE_CAPTURE_BIN_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_image_capture_bin_create_elements (imagebin)) {
        return GST_STATE_CHANGE_FAILURE;
      }

      /* set our image muxer to MERGE_REPLACE mode if it is a tagsetter */
      if (imagebin->muxer && gst_element_implements_interface (imagebin->muxer,
              GST_TYPE_TAG_SETTER)) {
        gst_tag_setter_set_tag_merge_mode (GST_TAG_SETTER (imagebin->muxer),
            GST_TAG_MERGE_REPLACE);
      }

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  switch (trans) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_image_capture_bin_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "imagecapturebin", GST_RANK_NONE,
      gst_image_capture_bin_get_type ());
}
