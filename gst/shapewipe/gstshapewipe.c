/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-shapewipe
 *
 * The shapewipe element provides custom transitions on video streams
 * based on a grayscale bitmap. The state of the transition can be
 * controlled by the position property and an optional blended border
 * can be added by the border property.
 *
 * Transition bitmaps can be downloaded from the
 * <ulink url="http://cinelerra.org/transitions.php">Cinelerra transition</ulink>
 * page.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! video/x-raw-yuv,width=640,height=480 ! shapewipe position=0.5 name=shape ! videomixer name=mixer ! ffmpegcolorspace ! autovideosink     filesrc location=mask.png ! typefind ! decodebin2 ! ffmpegcolorspace ! videoscale ! queue ! shape.mask_sink    videotestsrc pattern=snow ! video/x-raw-yuv,width=640,height=480 ! queue ! mixer.
 * ]| This pipeline adds the transition from mask.png with position 0.5 to an SMPTE test screen and snow.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>

#include "gstshapewipe.h"

static void gst_shape_wipe_finalize (GObject * object);
static void gst_shape_wipe_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_shape_wipe_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_shape_wipe_reset (GstShapeWipe * self);

static GstStateChangeReturn gst_shape_wipe_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_shape_wipe_video_sink_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_shape_wipe_video_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_shape_wipe_video_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstCaps *gst_shape_wipe_video_sink_getcaps (GstPad * pad);
static GstFlowReturn gst_shape_wipe_mask_sink_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_shape_wipe_mask_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_shape_wipe_mask_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_shape_wipe_mask_sink_getcaps (GstPad * pad);
static gboolean gst_shape_wipe_src_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_shape_wipe_src_getcaps (GstPad * pad);

enum
{
  PROP_0,
  PROP_POSITION,
  PROP_BORDER
};

static GstStaticPadTemplate video_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")));

static GstStaticPadTemplate mask_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("mask_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-gray, "
        "bpp = 8, "
        "depth = 8, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = 0/1 ; "
        "video/x-raw-gray, " "bpp = 16, " "depth = 16, "
        "endianness = BYTE_ORDER, " "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = 0/1"));

static GstStaticPadTemplate src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")));

GST_DEBUG_CATEGORY_STATIC (gst_shape_wipe_debug);
#define GST_CAT_DEFAULT gst_shape_wipe_debug

GST_BOILERPLATE (GstShapeWipe, gst_shape_wipe, GstElement, GST_TYPE_ELEMENT);

static void
gst_shape_wipe_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Shape Wipe transition filter",
      "Filter/Editor/Video",
      "Adds a shape wipe transition to a video stream",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_pad_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&mask_sink_pad_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_pad_template));
}

static void
gst_shape_wipe_class_init (GstShapeWipeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_shape_wipe_finalize;
  gobject_class->set_property = gst_shape_wipe_set_property;
  gobject_class->get_property = gst_shape_wipe_get_property;

  g_object_class_install_property (gobject_class, PROP_POSITION,
      g_param_spec_float ("position", "Position", "Position of the mask",
          0.0, 1.0, 0.0,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BORDER,
      g_param_spec_float ("border", "Border", "Border of the mask",
          0.0, 1.0, 0.0,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_shape_wipe_change_state);
}

static void
gst_shape_wipe_init (GstShapeWipe * self, GstShapeWipeClass * g_class)
{
  self->video_sinkpad =
      gst_pad_new_from_static_template (&video_sink_pad_template, "video_sink");
  gst_pad_set_chain_function (self->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_video_sink_chain));
  gst_pad_set_event_function (self->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_video_sink_event));
  gst_pad_set_setcaps_function (self->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_video_sink_setcaps));
  gst_pad_set_getcaps_function (self->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_video_sink_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->video_sinkpad);

  self->mask_sinkpad =
      gst_pad_new_from_static_template (&mask_sink_pad_template, "mask_sink");
  gst_pad_set_chain_function (self->mask_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_mask_sink_chain));
  gst_pad_set_event_function (self->mask_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_mask_sink_event));
  gst_pad_set_setcaps_function (self->mask_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_mask_sink_setcaps));
  gst_pad_set_getcaps_function (self->mask_sinkpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_mask_sink_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->mask_sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_pad_template, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_src_event));
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_shape_wipe_src_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->mask_mutex = g_mutex_new ();
  self->mask_cond = g_cond_new ();

  gst_shape_wipe_reset (self);
}

static void
gst_shape_wipe_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (object);

  switch (prop_id) {
    case PROP_POSITION:
      g_value_set_float (value, self->mask_position);
      break;
    case PROP_BORDER:
      g_value_set_float (value, self->mask_border);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_shape_wipe_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (object);

  switch (prop_id) {
    case PROP_POSITION:
      self->mask_position = g_value_get_float (value);
      break;
    case PROP_BORDER:
      self->mask_border = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_shape_wipe_finalize (GObject * object)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (object);

  gst_shape_wipe_reset (self);

  if (self->mask_cond)
    g_cond_free (self->mask_cond);
  self->mask_cond = NULL;

  if (self->mask_mutex)
    g_mutex_free (self->mask_mutex);
  self->mask_mutex = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_shape_wipe_reset (GstShapeWipe * self)
{
  if (self->mask)
    gst_buffer_unref (self->mask);
  self->mask = NULL;

  g_cond_signal (self->mask_cond);

  self->width = self->height = 0;
  self->mask_position = 0.0;
  self->mask_border = 0.0;
  self->mask_bpp = 0;

  gst_segment_init (&self->segment, GST_FORMAT_TIME);
}

static gboolean
gst_shape_wipe_video_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  GstStructure *s;
  gint width, height;

  GST_DEBUG_OBJECT (pad, "Setting caps: %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "width", &width) ||
      !gst_structure_get_int (s, "height", &height)) {
    ret = FALSE;
    goto done;
  }

  if (self->width != width || self->height != height) {
    g_mutex_lock (self->mask_mutex);
    self->width = width;
    self->height = height;

    if (self->mask)
      gst_buffer_unref (self->mask);
    self->mask = NULL;
    g_mutex_unlock (self->mask_mutex);
  }

  ret = gst_pad_set_caps (self->srcpad, caps);

done:
  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_shape_wipe_video_sink_getcaps (GstPad * pad)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  GstCaps *ret, *tmp;

  if (GST_PAD_CAPS (pad))
    return gst_caps_copy (GST_PAD_CAPS (pad));

  tmp = gst_pad_peer_get_caps (self->srcpad);
  if (tmp) {
    ret = gst_caps_intersect (tmp, gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (tmp);
  } else {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  tmp = gst_pad_peer_get_caps (pad);
  if (tmp) {
    GstCaps *intersection;

    intersection = gst_caps_intersect (tmp, ret);
    gst_caps_unref (tmp);
    gst_caps_unref (ret);
    ret = intersection;
  }

  if (self->height && self->width) {
    guint i, n;

    n = gst_caps_get_size (ret);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_set (s, "width", G_TYPE_INT, self->width, "height",
          G_TYPE_INT, self->height, NULL);
    }
  }

  tmp = gst_pad_peer_get_caps (self->mask_sinkpad);
  if (tmp) {
    GstCaps *intersection, *tmp2;
    guint i, n;

    tmp = gst_caps_make_writable (tmp);

    tmp2 = gst_caps_copy (gst_pad_get_pad_template_caps (self->mask_sinkpad));

    intersection = gst_caps_intersect (tmp, tmp2);
    gst_caps_unref (tmp);
    gst_caps_unref (tmp2);
    tmp = intersection;

    n = gst_caps_get_size (tmp);

    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (tmp, i);

      gst_structure_remove_fields (s, "bpp", "depth", "endianness", "framerate",
          NULL);
      gst_structure_set_name (s, "video/x-raw-yuv");
    }

    intersection = gst_caps_intersect (tmp, ret);
    gst_caps_unref (tmp);
    gst_caps_unref (ret);
    ret = intersection;
  }

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_shape_wipe_mask_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  GstStructure *s;
  gint width, height, bpp;

  GST_DEBUG_OBJECT (pad, "Setting caps: %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "width", &width) ||
      !gst_structure_get_int (s, "height", &height) ||
      !gst_structure_get_int (s, "bpp", &bpp)) {
    ret = FALSE;
    goto done;
  }

  if ((self->width != width || self->height != height) &&
      self->width > 0 && self->height > 0) {
    GST_ERROR_OBJECT (pad, "Mask caps must have the same width/height "
        "as the video caps");
    ret = FALSE;
    goto done;
  } else {
    self->width = width;
    self->height = height;
  }

  self->mask_bpp = bpp;

done:
  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_shape_wipe_mask_sink_getcaps (GstPad * pad)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  GstCaps *ret, *tmp;
  guint i, n;

  if (GST_PAD_CAPS (pad))
    return gst_caps_copy (GST_PAD_CAPS (pad));

  tmp = gst_pad_peer_get_caps (self->video_sinkpad);
  if (tmp) {
    ret =
        gst_caps_intersect (tmp,
        gst_pad_get_pad_template_caps (self->video_sinkpad));
    gst_caps_unref (tmp);
  } else {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (self->video_sinkpad));
  }

  tmp = gst_pad_peer_get_caps (self->srcpad);
  if (tmp) {
    GstCaps *intersection;

    intersection = gst_caps_intersect (ret, tmp);
    gst_caps_unref (ret);
    gst_caps_unref (tmp);
    ret = intersection;
  }

  n = gst_caps_get_size (ret);
  tmp = gst_caps_new_empty ();
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (ret, i);
    GstStructure *t;

    gst_structure_set_name (s, "video/x-raw-gray");
    gst_structure_remove_fields (s, "format", "framerate", NULL);

    if (self->width && self->height)
      gst_structure_set (s, "width", G_TYPE_INT, self->width, "height",
          G_TYPE_INT, self->height, NULL);

    gst_structure_set (s, "framerate", GST_TYPE_FRACTION, 0, 1, NULL);

    t = gst_structure_copy (s);

    gst_structure_set (s, "bpp", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16,
        "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
    gst_structure_set (t, "bpp", G_TYPE_INT, 8, "depth", G_TYPE_INT, 8, NULL);

    gst_caps_append_structure (tmp, t);
  }
  gst_caps_merge (ret, tmp);

  tmp = gst_pad_peer_get_caps (pad);
  if (tmp) {
    GstCaps *intersection;

    intersection = gst_caps_intersect (tmp, ret);
    gst_caps_unref (tmp);
    gst_caps_unref (ret);
    ret = intersection;
  }

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_shape_wipe_src_getcaps (GstPad * pad)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  GstCaps *ret, *tmp;

  if (GST_PAD_CAPS (pad))
    return gst_caps_copy (GST_PAD_CAPS (pad));
  else if (GST_PAD_CAPS (self->video_sinkpad))
    return gst_caps_copy (GST_PAD_CAPS (self->video_sinkpad));

  tmp = gst_pad_peer_get_caps (self->video_sinkpad);
  if (tmp) {
    ret =
        gst_caps_intersect (tmp,
        gst_pad_get_pad_template_caps (self->video_sinkpad));
    gst_caps_unref (tmp);
  } else {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (self->video_sinkpad));
  }

  tmp = gst_pad_peer_get_caps (pad);
  if (tmp) {
    GstCaps *intersection;

    intersection = gst_caps_intersect (tmp, ret);
    gst_caps_unref (tmp);
    gst_caps_unref (ret);
    ret = intersection;
  }

  if (self->height && self->width) {
    guint i, n;

    n = gst_caps_get_size (ret);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_set (s, "width", G_TYPE_INT, self->width, "height",
          G_TYPE_INT, self->height, NULL);
    }
  }

  tmp = gst_pad_peer_get_caps (self->mask_sinkpad);
  if (tmp) {
    GstCaps *intersection, *tmp2;
    guint i, n;

    tmp = gst_caps_make_writable (tmp);
    tmp2 = gst_caps_copy (gst_pad_get_pad_template_caps (self->mask_sinkpad));

    intersection = gst_caps_intersect (tmp, tmp2);
    gst_caps_unref (tmp);
    gst_caps_unref (tmp2);

    tmp = intersection;
    n = gst_caps_get_size (tmp);

    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (tmp, i);

      gst_structure_remove_fields (s, "bpp", "depth", "endianness", "framerate",
          NULL);
      gst_structure_set_name (s, "video/x-raw-yuv");
    }

    intersection = gst_caps_intersect (tmp, ret);
    gst_caps_unref (tmp);
    gst_caps_unref (ret);
    ret = intersection;
  }

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstFlowReturn
gst_shape_wipe_blend_16 (GstShapeWipe * self, GstBuffer * inbuf,
    GstBuffer * maskbuf, GstBuffer * outbuf)
{
  const guint16 *mask = (const guint16 *) GST_BUFFER_DATA (maskbuf);
  const guint8 *input = (const guint8 *) GST_BUFFER_DATA (inbuf);
  guint8 *output = (guint8 *) GST_BUFFER_DATA (outbuf);
  guint i, j;
  guint mask_increment = GST_ROUND_UP_2 (self->width) - self->width;
  gfloat position = self->mask_position;
  gfloat low = position - (self->mask_border / 2.0f);
  gfloat high = position + (self->mask_border / 2.0f);

  if (low < 0.0f) {
    high = 0.0f;
    low = 0.0f;
  }

  if (high > 1.0f) {
    low = 1.0f;
    high = 1.0f;
  }

  for (i = 0; i < self->height; i++) {
    for (j = 0; j < self->width; j++) {
      gfloat in = *mask / 65535.0f;

      if (in < low) {
        output[0] = 0x00;       /* A */
        output[1] = 0x00;       /* Y */
        output[2] = 0x80;       /* U */
        output[3] = 0x80;       /* V */
      } else if (in >= high) {
        output[0] = 0xff;       /* A */
        output[1] = input[1];   /* Y */
        output[2] = input[2];   /* U */
        output[3] = input[3];   /* V */
      } else {
        gfloat val = 255.0f * ((in - low) / (high - low));

        output[0] = CLAMP (val, 0, 255);        /* A */
        output[1] = input[1];   /* Y */
        output[2] = input[2];   /* U */
        output[3] = input[3];   /* V */
      }

      mask++;
      input += 4;
      output += 4;
    }
    mask += mask_increment;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_shape_wipe_blend_8 (GstShapeWipe * self, GstBuffer * inbuf,
    GstBuffer * maskbuf, GstBuffer * outbuf)
{
  const guint8 *mask = (const guint8 *) GST_BUFFER_DATA (maskbuf);
  const guint8 *input = (const guint8 *) GST_BUFFER_DATA (inbuf);
  guint8 *output = (guint8 *) GST_BUFFER_DATA (outbuf);
  guint i, j;
  guint mask_increment = GST_ROUND_UP_4 (self->width) - self->width;
  gfloat position = self->mask_position;
  gfloat low = position - (self->mask_border / 2.0f);
  gfloat high = position + (self->mask_border / 2.0f);

  if (low < 0.0f) {
    high = 0.0f;
    low = 0.0f;
  }

  if (high > 1.0f) {
    low = 1.0f;
    high = 1.0f;
  }

  for (i = 0; i < self->height; i++) {
    for (j = 0; j < self->width; j++) {
      gfloat in = *mask / 255.0f;

      if (in < low) {
        output[0] = 0x00;       /* A */
        output[1] = 0x00;       /* Y */
        output[2] = 0x80;       /* U */
        output[3] = 0x80;       /* V */
      } else if (in >= high) {
        output[0] = 0xff;       /* A */
        output[1] = input[1];   /* Y */
        output[2] = input[2];   /* U */
        output[3] = input[3];   /* V */
      } else {
        gfloat val = 255.0f * ((in - low) / (high - low));

        output[0] = CLAMP (val, 0, 255);        /* A */
        output[1] = input[1];   /* Y */
        output[2] = input[2];   /* U */
        output[3] = input[3];   /* V */
      }

      mask++;
      input += 4;
      output += 4;
    }
    mask += mask_increment;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_shape_wipe_video_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (GST_PAD_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *mask = NULL, *outbuf = NULL;
  GstClockTime timestamp;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  timestamp =
      gst_segment_to_stream_time (&self->segment, GST_FORMAT_TIME, timestamp);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (self), timestamp);

  GST_DEBUG_OBJECT (self,
      "Blending buffer with timestamp %" GST_TIME_FORMAT " at position %lf",
      GST_TIME_ARGS (timestamp), self->mask_position);

  g_mutex_lock (self->mask_mutex);
  mask = self->mask;
  if (self->mask)
    gst_buffer_ref (self->mask);
  else
    g_cond_wait (self->mask_cond, self->mask_mutex);

  if (self->mask == NULL) {
    g_mutex_unlock (self->mask_mutex);
    return GST_FLOW_UNEXPECTED;
  }

  mask = gst_buffer_ref (self->mask);

  g_mutex_unlock (self->mask_mutex);

  ret =
      gst_pad_alloc_buffer_and_set_caps (self->srcpad, GST_BUFFER_OFFSET_NONE,
      GST_BUFFER_SIZE (buffer), GST_PAD_CAPS (self->srcpad), &outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    return ret;

  if (self->mask_bpp == 16)
    ret = gst_shape_wipe_blend_16 (self, buffer, mask, outbuf);
  else
    ret = gst_shape_wipe_blend_8 (self, buffer, mask, outbuf);

  gst_buffer_unref (mask);
  gst_buffer_unref (buffer);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (outbuf);
    return ret;
  }

  ret = gst_pad_push (self->srcpad, outbuf);
  return ret;
}

static GstFlowReturn
gst_shape_wipe_mask_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (GST_PAD_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  g_mutex_lock (self->mask_mutex);
  GST_DEBUG_OBJECT (self, "Setting new mask buffer: %" GST_PTR_FORMAT, buffer);

  gst_buffer_replace (&self->mask, buffer);
  g_cond_signal (self->mask_cond);
  g_mutex_unlock (self->mask_mutex);

  return ret;
}

static GstStateChangeReturn
gst_shape_wipe_change_state (GstElement * element, GstStateChange transition)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    default:
      break;
  }

  /* Unblock video sink chain function */
  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    g_cond_signal (self->mask_cond);

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_shape_wipe_reset (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_shape_wipe_video_sink_event (GstPad * pad, GstEvent * event)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  gboolean ret;

  GST_DEBUG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gboolean is_update;
      gint64 start, end, base;
      gdouble rate;

      gst_event_parse_new_segment (event, &is_update, &rate, &fmt, &start,
          &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad,
            "Got NEWSEGMENT event in GST_FORMAT_TIME, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")", GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
        gst_segment_set_newsegment (&self->segment, is_update, rate, fmt, start,
            end, base);
      } else {
        gst_segment_init (&self->segment, GST_FORMAT_TIME);
      }
    }
      /* fall through */
    default:
      ret = gst_pad_push_event (self->srcpad, event);
      break;
  }

  gst_object_unref (self);
  return ret;
}

static gboolean
gst_shape_wipe_mask_sink_event (GstPad * pad, GstEvent * event)
{
  GST_DEBUG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  /* Dropping all events here */
  gst_event_unref (event);
  return TRUE;
}

static gboolean
gst_shape_wipe_src_event (GstPad * pad, GstEvent * event)
{
  GstShapeWipe *self = GST_SHAPE_WIPE (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = gst_pad_push_event (self->video_sinkpad, event);
      break;
  }

  gst_object_unref (self);
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_shape_wipe_debug, "shapewipe", 0,
      "shapewipe element");

  gst_controller_init (NULL, NULL);

  if (!gst_element_register (plugin, "shapewipe", GST_RANK_NONE,
          GST_TYPE_SHAPE_WIPE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "shapewipe",
    "Shape Wipe transition filter",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
