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
 * SECTION:camerabinimage
 * @short_description: image capturing module of #GstCameraBin
 *
 * <refsect2>
 * <para>
 *
 * The pipeline for this module is:
 *
 * <informalexample>
 * <programlisting>
 *-----------------------------------------------------------------------------
 *                      (src0) -> queue ->
 * -> [post proc] -> tee <
 *                      (src1) -> imageenc -> metadatamuxer -> filesink
 *-----------------------------------------------------------------------------
 * </programlisting>
 * </informalexample>
 *
 * The property of elements are:
 *
 *   queue - "max-size-buffers", 1, "leaky", 2,
 *
 * The image bin opens file for image writing in READY to PAUSED state change.
 * The image bin closes the file in PAUSED to READY state change.
 *
 * </para>
 * </refsect2>
 */

/*
 * includes
 */

#include <gst/gst.h>

#include "camerabinimage.h"
#include "camerabingeneral.h"

#include "string.h"

/* default internal element names */

#define DEFAULT_SINK "filesink"
#define DEFAULT_ENC "jpegenc"
#define DEFAULT_META_MUX "metadatamux"

enum
{
  PROP_0,
  PROP_FILENAME
};

static gboolean gst_camerabin_image_create_elements (GstCameraBinImage * img);
static void gst_camerabin_image_destroy_elements (GstCameraBinImage * img);

static void gst_camerabin_image_dispose (GstCameraBinImage * sink);
static GstStateChangeReturn
gst_camerabin_image_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_camerabin_image_send_event (GstElement * element,
    GstEvent * event);
static void gst_camerabin_image_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_camerabin_image_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstCameraBinImage, gst_camerabin_image, GstBin, GST_TYPE_BIN);

static const GstElementDetails gst_camerabin_image_details =
GST_ELEMENT_DETAILS ("Image capture bin for camerabin",
    "Bin/Image",
    "Process and store image data",
    "Edgard Lima <edgard.lima@indt.org.br>\n"
    "Nokia Corporation <multimedia@maemo.org>");

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_camerabin_image_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_details (eklass, &gst_camerabin_image_details);
}

static void
gst_camerabin_image_class_init (GstCameraBinImageClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose =
      (GObjectFinalizeFunc) GST_DEBUG_FUNCPTR (gst_camerabin_image_dispose);
  eklass->change_state = GST_DEBUG_FUNCPTR (gst_camerabin_image_change_state);
  eklass->send_event = GST_DEBUG_FUNCPTR (gst_camerabin_image_send_event);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_camerabin_image_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_camerabin_image_get_property);

  /**
   * GstCameraBinImage:filename
   *
   * This property can be used to specify the filename of the image.
   *
   **/
  g_object_class_install_property (gobject_class, PROP_FILENAME,
      g_param_spec_string ("filename", "Filename",
          "Filename of the image to save", NULL, G_PARAM_READWRITE));
}

static void
gst_camerabin_image_init (GstCameraBinImage * img,
    GstCameraBinImageClass * g_class)
{
  img->filename = g_string_new ("");

  img->pad_tee_enc = NULL;
  img->pad_tee_view = NULL;

  img->post = NULL;
  img->tee = NULL;
  img->enc = NULL;
  img->user_enc = NULL;
  img->meta_mux = NULL;
  img->sink = NULL;
  img->queue = NULL;

  /* Create src and sink ghost pads */
  img->sinkpad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (img), img->sinkpad);

  img->srcpad = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (img), img->srcpad);

  img->elements_created = FALSE;
}

static void
gst_camerabin_image_dispose (GstCameraBinImage * img)
{
  g_string_free (img->filename, TRUE);
  img->filename = NULL;

  if (img->user_enc) {
    gst_object_unref (img->user_enc);
    img->user_enc = NULL;
  }

  if (img->post) {
    gst_object_unref (img->post);
    img->post = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) img);
}

static GstStateChangeReturn
gst_camerabin_image_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCameraBinImage *img = GST_CAMERABIN_IMAGE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_camerabin_image_create_elements (img)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      /* Allow setting filename when image bin in READY state */
      gst_element_set_locked_state (img->sink, TRUE);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_element_set_locked_state (img->sink, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* Set sink to NULL in order to write the file _now_ */
      GST_INFO ("write img file: %s", img->filename->str);
      gst_element_set_locked_state (img->sink, TRUE);
      gst_element_set_state (img->sink, GST_STATE_NULL);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_camerabin_image_destroy_elements (img);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_camerabin_image_send_event (GstElement * element, GstEvent * event)
{
  GstCameraBinImage *bin = GST_CAMERABIN_IMAGE (element);
  gboolean ret = FALSE;

  GST_INFO ("got %s event", GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_IS_DOWNSTREAM (event)) {
    ret = gst_pad_send_event (bin->sinkpad, event);
  } else {
    if (bin->sink) {
      ret = gst_element_send_event (bin->sink, event);
    } else {
      GST_WARNING ("upstream event handling failed");
    }
  }

  return ret;
}

static void
gst_camerabin_image_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCameraBinImage *bin = GST_CAMERABIN_IMAGE (object);

  switch (prop_id) {
    case PROP_FILENAME:
      g_string_assign (bin->filename, g_value_get_string (value));
      if (bin->sink) {
        g_object_set (G_OBJECT (bin->sink), "location", bin->filename->str,
            NULL);
      } else {
        GST_INFO ("no sink, not setting name yet");
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camerabin_image_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraBinImage *bin = GST_CAMERABIN_IMAGE (object);

  switch (prop_id) {
    case PROP_FILENAME:
      g_value_set_string (value, bin->filename->str);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
 * static helper functions implementation
 */

/**
 * metadata_write_probe:
 * @pad: sink pad of metadata muxer
 * @buffer: received buffer
 * @u_data: image bin object
 *
 * Buffer probe that sets Xmp.dc.type and Xmp.dc.format tags
 * to metadata muxer based on preceding element src pad caps.
 *
 * Returns: TRUE always
 */
static gboolean
metadata_write_probe (GstPad * pad, GstBuffer * buffer, gpointer u_data)
{
  /* Add XMP tags */
  GstCameraBinImage *img = NULL;
  GstTagSetter *setter = NULL;
  GstPad *srcpad = NULL;
  GstCaps *caps = NULL;
  GstStructure *st = NULL;

  img = GST_CAMERABIN_IMAGE (u_data);

  g_return_val_if_fail (img != NULL, TRUE);

  setter = GST_TAG_SETTER (img->meta_mux);

  if (!setter) {
    GST_WARNING_OBJECT (img, "setting tags failed");
    goto done;
  }

  /* Xmp.dc.type tag */
  gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
      GST_TAG_CODEC, "Image", NULL);
  /* Xmp.dc.format tag */
  if (img->enc) {
    srcpad = gst_element_get_static_pad (img->enc, "src");
  }
  GST_LOG_OBJECT (img, "srcpad:%" GST_PTR_FORMAT, srcpad);
  if (srcpad) {
    caps = gst_pad_get_negotiated_caps (srcpad);
    GST_LOG_OBJECT (img, "caps:%" GST_PTR_FORMAT, caps);
    if (caps) {
      /* If there are many structures, we can't know which one to use */
      if (gst_caps_get_size (caps) != 1) {
        GST_WARNING_OBJECT (img, "can't decide structure for format tag");
        goto done;
      }
      st = gst_caps_get_structure (caps, 0);
      if (st) {
        GST_DEBUG_OBJECT (img, "Xmp.dc.format:%s", gst_structure_get_name (st));
        gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
            GST_TAG_VIDEO_CODEC, gst_structure_get_name (st), NULL);
      }
    }
  }
done:
  if (caps)
    gst_caps_unref (caps);
  if (srcpad)
    gst_object_unref (srcpad);

  return TRUE;
}


/**
 * gst_camerabin_image_create_elements:
 * @img: a pointer to #GstCameraBinImage object
 *
 * This function creates needed #GstElements and resources to capture images.
 * Use gst_camerabin_image_destroy_elements to release these resources.
 *
 * Image bin:
 *  img->sinkpad ! [ post process !] tee name=t0 ! encoder ! metadata ! filesink
 *   t0. ! queue ! img->srcpad
 *
 * Returns: %TRUE if succeeded or FALSE if failed
 */
static gboolean
gst_camerabin_image_create_elements (GstCameraBinImage * img)
{
  GstPad *sinkpad = NULL, *img_sinkpad = NULL, *img_srcpad = NULL;
  gboolean ret = FALSE;
  GstBin *imgbin = NULL;

  g_return_val_if_fail (img != NULL, FALSE);

  GST_DEBUG ("creating image capture elements");

  imgbin = GST_BIN (img);

  if (img->elements_created) {
    GST_WARNING ("elements already created");
    ret = TRUE;
    goto done;
  } else {
    img->elements_created = TRUE;
  }

  /* Create image pre/post-processing element if any */
  if (img->post) {
    if (!gst_camerabin_add_element (imgbin, img->post)) {
      goto done;
    }
    img_sinkpad = gst_element_get_static_pad (img->post, "sink");
  }

  /* Create tee */
  if (!(img->tee = gst_camerabin_create_and_add_element (imgbin, "tee"))) {
    goto done;
  }

  /* Set up sink ghost pad for img bin */
  if (!img_sinkpad) {
    img_sinkpad = gst_element_get_static_pad (img->tee, "sink");
  }
  gst_ghost_pad_set_target (GST_GHOST_PAD (img->sinkpad), img_sinkpad);

  /* Add colorspace converter */
  img->pad_tee_enc = gst_element_get_request_pad (img->tee, "src%d");
  if (!gst_camerabin_create_and_add_element (imgbin, "ffmpegcolorspace")) {
    goto done;
  }

  /* Create image encoder */
  if (img->user_enc) {
    img->enc = img->user_enc;
    if (!gst_camerabin_add_element (imgbin, img->enc)) {
      goto done;
    }
  } else if (!(img->enc =
          gst_camerabin_create_and_add_element (imgbin, DEFAULT_ENC))) {
    goto done;
  }

  /* Create metadata element */
  if (!(img->meta_mux =
          gst_camerabin_create_and_add_element (imgbin, DEFAULT_META_MUX))) {
    goto done;
  }
  /* Add probe for XMP metadata writing */
  sinkpad = gst_element_get_static_pad (img->meta_mux, "sink");
  gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (metadata_write_probe), img);
  gst_object_unref (sinkpad);
  /* Set "Intel" exif byte-order if possible */
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (img->meta_mux),
          "exif-byte-order")) {
    g_object_set (G_OBJECT (img->meta_mux), "exif-byte-order", 1, NULL);
  }

  /* Create file sink element */
  if (!(img->sink =
          gst_camerabin_create_and_add_element (imgbin, DEFAULT_SINK))) {
    goto done;
  }

  /* Create queue element leading to view finder, attaches it to the tee */
  img->pad_tee_view = gst_element_get_request_pad (img->tee, "src%d");
  if (!(img->queue = gst_camerabin_create_and_add_element (imgbin, "queue"))) {
    goto done;
  }

  /* Set properties */
  g_object_set (G_OBJECT (img->sink), "location", img->filename->str, NULL);
  g_object_set (G_OBJECT (img->sink), "async", FALSE, NULL);

  g_object_set (G_OBJECT (img->queue), "max-size-buffers", 1, "leaky", 2, NULL);

  /* Set up src ghost pad for img bin */
  img_srcpad = gst_element_get_static_pad (img->queue, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (img->srcpad), img_srcpad);

  /* Never let image bin eos events reach view finder */
  gst_pad_add_event_probe (img->srcpad,
      G_CALLBACK (gst_camerabin_drop_eos_probe), img);

  ret = TRUE;

done:

  if (img_srcpad) {
    gst_object_unref (img_srcpad);
  }
  if (img_sinkpad) {
    gst_object_unref (img_sinkpad);
  }
  if (!ret) {
    gst_camerabin_image_destroy_elements (img);
  }

  return ret;
}


/**
 * gst_camerabin_image_destroy_elements:
 * @img: a pointer to #GstCameraBinImage object
 *
 * This function releases resources allocated in
 * gst_camerabin_image_create_elements.
 *
 */
static void
gst_camerabin_image_destroy_elements (GstCameraBinImage * img)
{
  GST_LOG ("destroying img elements");
  if (img->pad_tee_enc) {
    gst_element_release_request_pad (img->tee, img->pad_tee_enc);
    img->pad_tee_enc = NULL;
  }

  if (img->pad_tee_view) {
    gst_element_release_request_pad (img->tee, img->pad_tee_view);
    img->pad_tee_view = NULL;
  }

  gst_ghost_pad_set_target (GST_GHOST_PAD (img->sinkpad), NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD (img->srcpad), NULL);

  gst_camerabin_remove_elements_from_bin (GST_BIN (img));

  img->post = NULL;
  img->tee = NULL;
  img->enc = NULL;
  img->meta_mux = NULL;
  img->sink = NULL;
  img->queue = NULL;

  img->elements_created = FALSE;
}

void
gst_camerabin_image_set_encoder (GstCameraBinImage * img, GstElement * encoder)
{
  if (img->user_enc)
    gst_object_unref (img->user_enc);
  if (encoder)
    gst_object_ref (encoder);

  img->user_enc = encoder;
}

void
gst_camerabin_image_set_postproc (GstCameraBinImage * img,
    GstElement * postproc)
{
  if (img->post)
    gst_object_unref (img->post);
  if (postproc)
    gst_object_ref (postproc);

  img->post = postproc;
}

GstElement *
gst_camerabin_image_get_encoder (GstCameraBinImage * img)
{
  GstElement *enc;

  if (img->user_enc) {
    enc = img->user_enc;
  } else {
    enc = img->enc;
  }

  return enc;
}

GstElement *
gst_camerabin_image_get_postproc (GstCameraBinImage * img)
{
  return img->post;
}
