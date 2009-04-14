/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstvdpdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_decoder_debug);
#define GST_CAT_DEFAULT gst_vdp_decoder_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_SILENT
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/vdpau-video, " "chroma-type = (int) 0"));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_decoder_debug, "vdpaudecoder", 0, "vdpaudecoder base class");

GST_BOILERPLATE_FULL (GstVdpDecoder, gst_vdp_decoder, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_vdp_decoder_finalize (GObject * object);
static void gst_vdp_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vdp_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GstFlowReturn
gst_vdp_decoder_push_video_buffer (GstVdpDecoder * dec,
    GstVdpVideoBuffer * buffer)
{
  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (GST_SECOND * dec->frame_nr,
      dec->framerate_denominator, dec->framerate_numerator);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (GST_SECOND, dec->framerate_denominator,
      dec->framerate_numerator);
  GST_BUFFER_OFFSET (buffer) = dec->frame_nr;
  dec->frame_nr++;
  GST_BUFFER_OFFSET_END (buffer) = dec->frame_nr;
  gst_buffer_set_caps (GST_BUFFER (buffer), GST_PAD_CAPS (dec->src));

  return gst_pad_push (dec->src, GST_BUFFER (buffer));
}

static GstStateChangeReturn
gst_vdp_decoder_change_state (GstElement * element, GstStateChange transition)
{
  GstVdpDecoder *dec;

  dec = GST_VDPAU_DECODER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      dec->device = gst_vdp_get_device (dec->display_name);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_object_unref (dec->device);
      dec->device = NULL;
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_vdp_decoder_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstVdpDecoder *dec = GST_VDPAU_DECODER (GST_OBJECT_PARENT (pad));
  GstVdpDecoderClass *dec_class = GST_VDPAU_DECODER_GET_CLASS (dec);

  GstCaps *src_caps, *new_caps;
  GstStructure *structure;
  gint width, height;
  gint framerate_numerator, framerate_denominator;
  gint par_numerator, par_denominator;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_fraction (structure, "framerate",
      &framerate_numerator, &framerate_denominator);
  gst_structure_get_fraction (structure, "pixel-aspect-ratio",
      &par_numerator, &par_denominator);

  src_caps = gst_pad_get_allowed_caps (dec->src);
  if (G_UNLIKELY (!src_caps))
    return FALSE;

  new_caps = gst_caps_copy_nth (src_caps, 0);
  gst_caps_unref (src_caps);
  structure = gst_caps_get_structure (new_caps, 0);
  gst_structure_set (structure,
      "device", G_TYPE_OBJECT, dec->device,
      "chroma-type", G_TYPE_INT, VDP_CHROMA_TYPE_420,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, framerate_numerator,
      framerate_denominator,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, par_numerator,
      par_denominator, NULL);

  gst_pad_fixate_caps (dec->src, new_caps);
  res = gst_pad_set_caps (dec->src, new_caps);

  gst_caps_unref (new_caps);

  if (G_UNLIKELY (!res))
    return FALSE;

  dec->width = width;
  dec->height = height;
  dec->framerate_numerator = framerate_numerator;
  dec->framerate_denominator = framerate_denominator;

  if (dec_class->set_caps && !dec_class->set_caps (dec, caps))
    return FALSE;

  return TRUE;
}

/* GObject vmethod implementations */

static void
gst_vdp_decoder_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

/* initialize the vdpaudecoder's class */
static void
gst_vdp_decoder_class_init (GstVdpDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_vdp_decoder_finalize;
  gobject_class->set_property = gst_vdp_decoder_set_property;
  gobject_class->get_property = gst_vdp_decoder_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gstelement_class->change_state = gst_vdp_decoder_change_state;
}

static void
gst_vdp_decoder_init (GstVdpDecoder * dec, GstVdpDecoderClass * klass)
{
  dec->display_name = NULL;
  dec->device = NULL;
  dec->silent = FALSE;

  dec->height = 0;
  dec->width = 0;
  dec->framerate_numerator = 0;
  dec->framerate_denominator = 0;

  dec->frame_nr = 0;

  dec->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->src);

  dec->sink = gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink"), "sink");
  gst_pad_set_setcaps_function (dec->sink, gst_vdp_decoder_sink_set_caps);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sink);
  gst_pad_set_active (dec->sink, TRUE);
}

static void
gst_vdp_decoder_finalize (GObject * object)
{
  GstVdpDecoder *dec = (GstVdpDecoder *) object;

  if (dec->device)
    g_object_unref (dec->device);

  g_free (dec->display_name);
}

static void
gst_vdp_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpDecoder *dec = GST_VDPAU_DECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (dec->display_name);
      dec->display_name = g_value_dup_string (value);
      break;
    case PROP_SILENT:
      dec->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpDecoder *dec = GST_VDPAU_DECODER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, dec->display_name);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, dec->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
