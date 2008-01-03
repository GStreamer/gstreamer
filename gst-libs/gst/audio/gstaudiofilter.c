/* GStreamer audio filter base class
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2007> Tim-Philipp Müller <tim centricular net>
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
 * SECTION:gstaudiofilter
 * @short_description: Base class for simple audio filters
 *
 * #GstAudioFilter is a #GstBaseTransform-derived base class for simple audio
 * filters, ie. those that output the same format that they get as input.
 *
 * #GstAudioFilter will parse the input format for you (with error checking)
 * before calling your setup function. Also, elements deriving from
 * #GstAudioFilter may use gst_audio_filter_class_add_pad_templates() from
 * their base_init function to easily configure the set of caps/formats that
 * the element is able to handle.
 *
 * Derived classes should override the GstAudioFilter::setup() and
 * GstBaseTransform::transform_ip() and/or GstBaseTransform::transform()
 * virtual functions in their class_init function.
 *
 * Since: 0.10.12
 *
 * Last reviewed on 2007-02-03 (0.10.11.1)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaudiofilter.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (audiofilter_dbg);
#define GST_CAT_DEFAULT audiofilter_dbg

static void gst_audio_filter_class_init (gpointer g_class, gpointer class_data);
static void gst_audio_filter_init (GTypeInstance * instance, gpointer g_class);
static GstStateChangeReturn gst_audio_filter_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_audio_filter_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_audio_filter_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, guint * size);

static GstElementClass *parent_class = NULL;

GType
gst_audio_filter_get_type (void)
{
  static GType audio_filter_type = 0;

  if (!audio_filter_type) {
    const GTypeInfo audio_filter_info = {
      sizeof (GstAudioFilterClass),
      NULL,
      NULL,
      gst_audio_filter_class_init,
      NULL,
      NULL,
      sizeof (GstAudioFilter),
      0,
      gst_audio_filter_init,
    };

    GST_DEBUG_CATEGORY_INIT (audiofilter_dbg, "audiofilter", 0, "audiofilter");

    audio_filter_type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstAudioFilter", &audio_filter_info, G_TYPE_FLAG_ABSTRACT);
  }
  return audio_filter_type;
}

static void
gst_audio_filter_class_init (gpointer klass, gpointer class_data)
{
  GstBaseTransformClass *basetrans_class;
  GstElementClass *gstelement_class;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class = (GstElementClass *) klass;
  basetrans_class = (GstBaseTransformClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_audio_filter_change_state);
  basetrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_audio_filter_set_caps);
  basetrans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_audio_filter_get_unit_size);
}

static void
gst_audio_filter_init (GTypeInstance * instance, gpointer g_class)
{
  /* nothing to do here */
}

/* we override the state change vfunc here instead of GstBaseTransform's stop
 * vfunc, so GstAudioFilter-derived elements can override ::stop() for their
 * own purposes without having to worry about chaining up */
static GstStateChangeReturn
gst_audio_filter_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAudioFilter *filter;

  filter = GST_AUDIO_FILTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      memset (&filter->format, 0, sizeof (GstRingBufferSpec));
      /* to make gst_buffer_spec_parse_caps() happy */
      filter->format.latency_time = GST_SECOND;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_caps_replace (&filter->format.caps, NULL);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_audio_filter_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAudioFilterClass *klass;
  GstAudioFilter *filter;
  gboolean ret = TRUE;

  g_assert (gst_caps_is_equal (incaps, outcaps));

  filter = GST_AUDIO_FILTER (btrans);

  GST_LOG_OBJECT (filter, "caps: %" GST_PTR_FORMAT, incaps);

  if (!gst_ring_buffer_parse_caps (&filter->format, incaps)) {
    GST_WARNING_OBJECT (filter, "couldn't parse %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  klass = GST_AUDIO_FILTER_CLASS (G_OBJECT_GET_CLASS (filter));

  if (klass->setup)
    ret = klass->setup (filter, &filter->format);

  return ret;
}

static gboolean
gst_audio_filter_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstStructure *structure;
  gboolean ret = TRUE;
  gint width, channels;

  structure = gst_caps_get_structure (caps, 0);

  ret &= gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "channels", &channels);

  if (ret)
    *size = (width / 8) * channels;

  return ret;
}

/**
 * gst_audio_filter_class_add_pad_templates:
 * @klass: an #GstAudioFilterClass
 * @allowed_caps: what formats the filter can handle, as #GstCaps
 *
 * Convenience function to add pad templates to this element class, with
 * @allowed_caps as the caps that can be handled.
 *
 * This function is usually used from within a GObject base_init function.
 *
 * Since: 0.10.12
 */
void
gst_audio_filter_class_add_pad_templates (GstAudioFilterClass * klass,
    const GstCaps * allowed_caps)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  g_return_if_fail (GST_IS_AUDIO_FILTER_CLASS (klass));
  g_return_if_fail (allowed_caps != NULL);
  g_return_if_fail (GST_IS_CAPS (allowed_caps));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_copy (allowed_caps)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_caps_copy (allowed_caps)));
}
