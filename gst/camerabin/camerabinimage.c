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
 *
 * -> [post proc] -> csp -> imageenc -> metadatamuxer -> filesink
 *
 *-----------------------------------------------------------------------------
 * </programlisting>
 * </informalexample>
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
#include "camerabindebug.h"
#include "camerabingeneral.h"
#include "gstcamerabin-enum.h"

#include "string.h"

/* default internal element names */

#define DEFAULT_SINK "filesink"
#define DEFAULT_ENC "jpegenc"
#define DEFAULT_FORMATTER "jifmux"
#define DEFAULT_FLAGS GST_CAMERABIN_FLAG_IMAGE_COLOR_CONVERSION

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
static gboolean metadata_write_probe (GstPad * pad, GstBuffer * buffer,
    gpointer u_data);
static gboolean prepare_element (GList ** result,
    const gchar * default_element_name, GstElement * app_elem,
    GstElement ** res_elem);


GST_BOILERPLATE (GstCameraBinImage, gst_camerabin_image, GstBin, GST_TYPE_BIN);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_camerabin_image_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (eklass, &sink_template);
  gst_element_class_set_details_simple (eklass,
      "Image capture bin for camerabin", "Bin/Image",
      "Process and store image data",
      "Edgard Lima <edgard.lima@indt.org.br>, "
      "Nokia Corporation <multimedia@maemo.org>");
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
          "Filename of the image to save", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_camerabin_image_init (GstCameraBinImage * img,
    GstCameraBinImageClass * g_class)
{
  img->filename = g_string_new ("");

  img->post = NULL;
  img->csp = NULL;
  img->enc = NULL;
  img->app_enc = NULL;
  img->formatter = NULL;
  img->app_formatter = NULL;
  img->sink = NULL;

  /* Create src and sink ghost pads */
  img->sinkpad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (img), img->sinkpad);

  img->flags = DEFAULT_FLAGS;
}

static void
gst_camerabin_image_dispose (GstCameraBinImage * img)
{
  GST_DEBUG_OBJECT (img, "disposing");

  g_string_free (img->filename, TRUE);
  img->filename = NULL;

  if (img->elements) {
    g_list_free (img->elements);
    img->elements = NULL;
  }

  if (img->sink) {
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->sink), GST_OBJECT_REFCOUNT_VALUE (img->sink));
    gst_object_unref (img->sink);
    img->sink = NULL;
  }

  if (img->formatter) {
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->formatter),
        GST_OBJECT_REFCOUNT_VALUE (img->formatter));
    gst_object_unref (img->formatter);
    img->formatter = NULL;
  }

  if (img->app_formatter) {
    gst_object_sink (img->app_formatter);
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->app_formatter),
        GST_OBJECT_REFCOUNT_VALUE (img->app_formatter));
    gst_object_unref (img->app_formatter);
    img->app_formatter = NULL;
  }

  if (img->enc) {
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->enc), GST_OBJECT_REFCOUNT_VALUE (img->enc));
    gst_object_unref (img->enc);
    img->enc = NULL;
  }

  if (img->csp) {
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->csp), GST_OBJECT_REFCOUNT_VALUE (img->csp));
    gst_object_unref (img->csp);
    img->csp = NULL;
  }

  /* Note: if imagebin was never set to READY state the
     ownership of elements created by application were never
     taken by bin and therefore gst_object_sink is called for
     these elements (they may still be in floating state
     and not unreffed properly without sinking first)
   */
  if (img->app_enc) {
    gst_object_sink (img->app_enc);
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->app_enc),
        GST_OBJECT_REFCOUNT_VALUE (img->app_enc));
    gst_object_unref (img->app_enc);
    img->app_enc = NULL;
  }

  if (img->post) {
    gst_object_sink (img->post);
    GST_LOG_OBJECT (img, "disposing %s with refcount %d",
        GST_ELEMENT_NAME (img->post), GST_OBJECT_REFCOUNT_VALUE (img->post));
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

  GST_DEBUG_OBJECT (element, "changing state: %s -> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_camerabin_image_create_elements (img)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      /* Allow setting filename when image bin in READY state */
      gst_element_set_locked_state (img->sink, TRUE);
      GST_INFO_OBJECT (img, "locking imagebin->sink state to %s",
          gst_element_state_get_name (GST_STATE (img->sink)));
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!g_str_equal (img->filename->str, "")) {
        GST_INFO_OBJECT (img, "preparing image with filename: %s",
            img->filename->str);
        gst_element_set_locked_state (img->sink, FALSE);
      } else {
        GST_INFO_OBJECT (img, "keep sink locked, we have no filename yet");
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* Set sink to NULL in order to write the file _now_ */
      GST_INFO_OBJECT (img, "write image with filename: %s",
          img->filename->str);
      gst_element_set_locked_state (img->sink, TRUE);
      gst_element_set_state (img->sink, GST_STATE_NULL);
      g_string_assign (img->filename, "");
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* Write debug graph to file */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (GST_ELEMENT_PARENT (img)),
          GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE |
          GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "imagebin.playing");
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_camerabin_image_destroy_elements (img);
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (element, "changed state: %s -> %s = %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)),
      gst_element_state_change_return_get_name (ret));

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
      GST_INFO_OBJECT (bin, "received filename: '%s'", bin->filename->str);
      if (bin->sink) {
        if (!g_str_equal (bin->filename->str, "")) {
          g_object_set (G_OBJECT (bin->sink), "location", bin->filename->str,
              NULL);
          gst_element_set_locked_state (bin->sink, FALSE);
          gst_element_sync_state_with_parent (bin->sink);
        } else {
          GST_INFO_OBJECT (bin, "empty filename");
        }
      } else {
        GST_INFO_OBJECT (bin, "no sink, not setting name yet");
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
 * gst_camerabin_image_prepare_elements:
 * @imagebin: a pointer to #GstCameraBinImage object
 *
 * This function creates an ordered list of elements configured for imagebin
 * pipeline and creates the elements if necessary. It also stores pointers
 * to created elements for re-using them.
 *
 * Image bin:
 *  img->sinkpad ! [ post process !] [ csp !] encoder ! metadata ! filesink
 *
 * Returns: %FALSE if there was error creating element, %TRUE otherwise
 */
gboolean
gst_camerabin_image_prepare_elements (GstCameraBinImage * imagebin)
{
  gboolean ret = FALSE;
  GstPad *sinkpad = NULL;

  g_return_val_if_fail (imagebin != NULL, FALSE);

  GST_DEBUG_OBJECT (imagebin, "preparing image capture elements");

  if (imagebin->elements != NULL) {
    g_list_free (imagebin->elements);
    imagebin->elements = NULL;
  }

  /* Create file sink element */
  if (!prepare_element (&imagebin->elements, DEFAULT_SINK, NULL,
          &imagebin->sink)) {
    goto done;
  } else {
    g_object_set (G_OBJECT (imagebin->sink), "location",
        imagebin->filename->str, "async", FALSE, "buffer-mode", 2,
        /* non buffered io */ NULL);
  }

  /* Create metadata muxer element */
  if (!prepare_element (&imagebin->elements, DEFAULT_FORMATTER,
          imagebin->app_formatter, &imagebin->formatter)) {
    goto done;
  } else if (!imagebin->metadata_probe_id) {
    /* Add probe for default XMP metadata writing */
    sinkpad = gst_element_get_static_pad (imagebin->formatter, "sink");
    imagebin->metadata_probe_id =
        gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (metadata_write_probe),
        imagebin);
    gst_object_unref (sinkpad);
  }

  /* Create image encoder element */
  if (!prepare_element (&imagebin->elements, DEFAULT_ENC, imagebin->app_enc,
          &imagebin->enc)) {
    goto done;
  }

  /* Create optional colorspace conversion element */
  if (imagebin->flags & GST_CAMERABIN_FLAG_IMAGE_COLOR_CONVERSION) {
    if (!prepare_element (&imagebin->elements, "ffmpegcolorspace", NULL,
            &imagebin->csp)) {
      goto done;
    }
  }

  /* Add optional image post processing element */
  if (!prepare_element (&imagebin->elements, NULL, imagebin->post,
          &imagebin->post)) {
    goto done;
  }

  ret = TRUE;

done:
  GST_DEBUG_OBJECT (imagebin, "preparing finished %s", ret ? "OK" : "NOK");
  return ret;
}


/*
 * static helper functions implementation
 */

/*
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

  if (GST_IS_TAG_SETTER (img->formatter)) {
    setter = GST_TAG_SETTER (img->formatter);
  }

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

/*
 * prepare_element:
 * @result: result list address
 * @default_element_name: name of default element to be created
 * @app_elem: pointer to application set element
 * @res_elem: pointer to current element to be replaced if needed
 *
 * This function chooses given image capture element or creates a new one and
 * and prepends it to @result list.
 *
 * Returns: %FALSE if there was error creating new element, %TRUE otherwise
 */
static gboolean
prepare_element (GList ** result, const gchar * default_element_name,
    GstElement * app_elem, GstElement ** res_elem)
{
  GstElement *elem = NULL;
  gboolean ret = TRUE;

  if (app_elem) {
    /* Prefer application set element */
    elem = app_elem;
  } else if (*res_elem) {
    /* Use existing element if any */
    elem = *res_elem;
  } else if (default_element_name) {
    /* Create new element */
    if (!(elem = gst_element_factory_make (default_element_name, NULL))) {
      GST_WARNING ("creating %s failed", default_element_name);
      ret = FALSE;
    }
  }

  if (*res_elem != elem) {
    /* Keep reference and store pointer to chosen element, which can be re-used
       until imagebin is disposed or new image capture element is chosen. */
    gst_object_replace ((GstObject **) res_elem, (GstObject *) elem);
  }
  if (elem) {
    *result = g_list_prepend (*result, elem);
  }

  return ret;
}

/*
 * gst_camerabin_image_link_first_element:
 * @img: a pointer to #GstCameraBinImage object
 * @elem: first element to be linked on imagebin
 *
 * Adds given element to imagebin and links it to imagebin's ghost sink pad.
 *
 * Returns: %TRUE if adding and linking succeeded, %FALSE otherwise
 */
static gboolean
gst_camerabin_image_link_first_element (GstCameraBinImage * imagebin,
    GstElement * elem)
{
  GstPad *first_sinkpad = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (imagebin != NULL, FALSE);
  /* Link given element to imagebin ghost sink pad */
  if (gst_bin_add (GST_BIN (imagebin), elem)) {
    first_sinkpad = gst_element_get_static_pad (elem, "sink");
    if (first_sinkpad) {
      if (gst_ghost_pad_set_target (GST_GHOST_PAD (imagebin->sinkpad),
              first_sinkpad)) {
        ret = TRUE;
      } else {
        GST_WARNING ("linking first element failed");
      }
      gst_object_unref (first_sinkpad);
    } else {
      GST_WARNING ("no sink pad in first element");
    }
  } else {
    GST_WARNING ("adding element failed");
  }
  return ret;
}

/*
 * gst_camerabin_image_link_elements:
 * @imagebin: a pointer to #GstCameraBinImage object
 *
 * Link elements configured to imagebin elements list.
 *
 * Returns %TRUE if linking succeeded, %FALSE otherwise.
 */
static gboolean
gst_camerabin_image_link_elements (GstCameraBinImage * imagebin)
{
  GList *prev = NULL;
  GList *next = NULL;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (imagebin, "linking image elements");

  if (!imagebin->elements) {
    GST_WARNING ("no elements to link");
    goto done;
  }

  /* Link the elements in list */
  prev = imagebin->elements;
  next = g_list_next (imagebin->elements);
  for (; next != NULL; next = g_list_next (next)) {
    /* Link first element in list to imagebin ghost sink pad */
    if (prev == imagebin->elements
        && !gst_camerabin_image_link_first_element (imagebin,
            GST_ELEMENT (prev->data))) {
      goto done;
    }
    if (!gst_bin_add (GST_BIN (imagebin), GST_ELEMENT (next->data))) {
      GST_WARNING_OBJECT (imagebin, "adding element failed");
      goto done;
    }
    GST_LOG_OBJECT (imagebin, "linking %s - %s",
        GST_ELEMENT_NAME (GST_ELEMENT (prev->data)),
        GST_ELEMENT_NAME (GST_ELEMENT (next->data)));
    if (!gst_element_link (GST_ELEMENT (prev->data), GST_ELEMENT (next->data))) {
      GST_WARNING_OBJECT (imagebin, "linking element failed");
      goto done;
    }

    prev = next;
  }

  ret = TRUE;

done:

  if (!ret) {
    gst_camerabin_remove_elements_from_bin (GST_BIN (imagebin));
  }

  GST_DEBUG_OBJECT (imagebin, "linking finished %s", ret ? "OK" : "NOK");

  return ret;
}

/*
 * gst_camerabin_image_create_elements:
 * @img: a pointer to #GstCameraBinImage object
 *
 * This function creates needed elements, adds them to
 * imagebin and links them.
 *
 * Returns %TRUE if success, %FALSE otherwise.
 */
static gboolean
gst_camerabin_image_create_elements (GstCameraBinImage * img)
{
  gboolean ret = FALSE;
  g_return_val_if_fail (img != NULL, FALSE);

  if (gst_camerabin_image_prepare_elements (img)) {
    ret = gst_camerabin_image_link_elements (img);
  }

  return ret;
}

/*
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
  GST_LOG ("destroying image elements");

  gst_ghost_pad_set_target (GST_GHOST_PAD (img->sinkpad), NULL);

  gst_camerabin_remove_elements_from_bin (GST_BIN (img));
}

void
gst_camerabin_image_set_encoder (GstCameraBinImage * img, GstElement * encoder)
{
  GST_DEBUG ("setting image encoder %" GST_PTR_FORMAT, encoder);
  if (img->app_enc)
    gst_object_unref (img->app_enc);
  if (encoder)
    gst_object_ref (encoder);

  img->app_enc = encoder;
}

void
gst_camerabin_image_set_postproc (GstCameraBinImage * img,
    GstElement * postproc)
{
  GST_DEBUG ("setting image postprocessing element %" GST_PTR_FORMAT, postproc);
  if (img->post)
    gst_object_unref (img->post);
  if (postproc)
    gst_object_ref (postproc);

  img->post = postproc;
}

void
gst_camerabin_image_set_formatter (GstCameraBinImage * img,
    GstElement * formatter)
{
  GstElement **app_formatter;
  GST_DEBUG ("setting image formatter %" GST_PTR_FORMAT, formatter);

  app_formatter = &img->app_formatter;
  GST_OBJECT_LOCK (img);
  gst_object_replace ((GstObject **) app_formatter, GST_OBJECT (formatter));
  GST_OBJECT_UNLOCK (img);
}

void
gst_camerabin_image_set_flags (GstCameraBinImage * img, GstCameraBinFlags flags)
{
  GST_DEBUG_OBJECT (img, "setting image flags: %d", flags);
  img->flags = flags;
}

GstElement *
gst_camerabin_image_get_encoder (GstCameraBinImage * img)
{
  GstElement *enc;

  if (img->app_enc) {
    enc = img->app_enc;
  } else {
    enc = img->enc;
  }

  return enc;
}

GstElement *
gst_camerabin_image_get_formatter (GstCameraBinImage * img)
{
  /* Prefer formatter that is currently in use */
  return img->formatter ? img->formatter : img->app_formatter;
}

GstElement *
gst_camerabin_image_get_postproc (GstCameraBinImage * img)
{
  return img->post;
}
