/* GStreamer
 * Copyright (C) <2009> Jan Schmidt <thaytan@noraisin.net>
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

#include <string.h>

#include "rsnaudiodec.h"

GST_DEBUG_CATEGORY_STATIC (rsn_audiodec_debug);
#define GST_CAT_DEFAULT rsn_audiodec_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg,mpegversion=(int)1;"
        "audio/x-private1-lpcm;"
        "audio/x-private1-ac3;" "audio/ac3;" "audio/x-ac3;"
        "audio/x-private1-dts;")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 32, 64 }; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 24, "
        "depth = (int) 24, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) true")
    );

G_DEFINE_TYPE (RsnAudioDec, rsn_audiodec, GST_TYPE_BIN);

static GstStateChangeReturn rsn_audiodec_change_state (GstElement * element,
    GstStateChange transition);

static void rsn_audiodec_dispose (GObject * gobj);
static void cleanup_child (RsnAudioDec * self);

static GList *decoder_factories = NULL;
static GstCaps *decoder_caps = NULL;

static void
rsn_audiodec_class_init (RsnAudioDecClass * klass)
{
  static GstElementDetails element_details = {
    "RsnAudioDec",
    "Audio/Decoder",
    "Resin DVD audio stream decoder",
    "Jan Schmidt <thaytan@noraisin.net>"
  };
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (rsn_audiodec_debug, "rsnaudiodec",
      0, "Resin DVD audio stream decoder");

  object_class->dispose = rsn_audiodec_dispose;

  element_class->change_state = GST_DEBUG_FUNCPTR (rsn_audiodec_change_state);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &element_details);
}

static gboolean
rsn_audiodec_sink_event (GstPad * pad, GstEvent * event)
{
  RsnAudioDec *self = RSN_AUDIODEC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  const GstStructure *s = gst_event_get_structure (event);
  const gchar *name = (s ? gst_structure_get_name (s) : NULL);

  if (name && g_str_equal (name, "application/x-gst-dvd"))
    ret = gst_pad_push_event (GST_PAD_CAST (self->srcpad), event);
  else
    ret = self->sink_event_func (pad, event);

  gst_object_unref (self);

  return ret;
}

static void
rsn_audiodec_init (RsnAudioDec * self)
{
  GstPadTemplate *templ;

  templ = gst_static_pad_template_get (&sink_template);
  self->sinkpad =
      GST_GHOST_PAD_CAST (gst_ghost_pad_new_no_target_from_template ("sink",
          templ));
  gst_object_unref (templ);
  self->sink_event_func = GST_PAD_EVENTFUNC (self->sinkpad);
  gst_pad_set_event_function (GST_PAD_CAST (self->sinkpad),
      GST_DEBUG_FUNCPTR (rsn_audiodec_sink_event));
  templ = gst_static_pad_template_get (&src_template);
  self->srcpad =
      GST_GHOST_PAD_CAST (gst_ghost_pad_new_no_target_from_template ("src",
          templ));
  gst_object_unref (templ);
  gst_element_add_pad (GST_ELEMENT (self), GST_PAD_CAST (self->sinkpad));
  gst_element_add_pad (GST_ELEMENT (self), GST_PAD_CAST (self->srcpad));
}

static void
rsn_audiodec_dispose (GObject * object)
{
  RsnAudioDec *self = (RsnAudioDec *) object;
  cleanup_child (self);

  G_OBJECT_CLASS (rsn_audiodec_parent_class)->dispose (object);
}

static gboolean
rsn_audiodec_set_child (RsnAudioDec * self, GstElement * new_child)
{
  GstPad *child_pad;
  if (self->current_decoder) {
    gst_ghost_pad_set_target (self->srcpad, NULL);
    gst_ghost_pad_set_target (self->sinkpad, NULL);
    gst_bin_remove ((GstBin *) self, self->current_decoder);
    self->current_decoder = NULL;
  }

  if (new_child == NULL)
    return TRUE;

  if (!gst_bin_add ((GstBin *) self, new_child))
    return FALSE;

  child_pad = gst_element_get_static_pad (new_child, "sink");
  if (child_pad == NULL) {
    return FALSE;
  }
  gst_ghost_pad_set_target (self->sinkpad, child_pad);
  gst_object_unref (child_pad);

  child_pad = gst_element_get_static_pad (new_child, "src");
  if (child_pad == NULL) {
    return FALSE;
  }
  gst_ghost_pad_set_target (self->srcpad, child_pad);
  gst_object_unref (child_pad);

  GST_DEBUG_OBJECT (self, "Add child %" GST_PTR_FORMAT, new_child);
  self->current_decoder = new_child;

  gst_element_sync_state_with_parent (new_child);

  return TRUE;
}

static void
cleanup_child (RsnAudioDec * self)
{
  GST_DEBUG_OBJECT (self, "Removing child element");
  (void) rsn_audiodec_set_child (self, NULL);
}

static gboolean
rsnaudiodec_factory_filter (GstPluginFeature * feature, GstCaps * desired_caps)
{
  GstElementFactory *factory;
  guint rank;
  const gchar *klass;
  const GList *templates;
  GList *walk;
  gboolean can_sink = FALSE;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  factory = GST_ELEMENT_FACTORY (feature);

  klass = gst_element_factory_get_klass (factory);
  /* only decoders can play */
  if (strstr (klass, "Decoder") == NULL)
    return FALSE;

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  /* See if the element has a sink pad that can possibly sink this caps */

  /* get the templates from the element factory */
  templates = gst_element_factory_get_static_pad_templates (factory);
  for (walk = (GList *) templates; walk && !can_sink; walk = g_list_next (walk)) {
    GstStaticPadTemplate *templ = walk->data;

    /* we only care about the sink templates */
    if (templ->direction == GST_PAD_SINK) {
      GstCaps *intersect;
      GstCaps *tmpl_caps;

      /* try to intersect the caps with the caps of the template */
      tmpl_caps = gst_static_caps_get (&templ->static_caps);

      intersect = gst_caps_intersect (desired_caps, tmpl_caps);
      gst_caps_unref (tmpl_caps);

      /* check if the intersection is empty */
      if (!gst_caps_is_empty (intersect)) {
        GstCaps *new_dec_caps;
        /* non empty intersection, we can use this element */
        can_sink = TRUE;
        new_dec_caps = gst_caps_union (decoder_caps, intersect);
        gst_caps_unref (decoder_caps);
        decoder_caps = new_dec_caps;
      }
      gst_caps_unref (intersect);
    }
  }

  if (can_sink) {
    GST_DEBUG ("Found decoder element %s (%s)",
        gst_element_factory_get_longname (factory),
        gst_plugin_feature_get_name (feature));
  }

  return can_sink;
}

static gint
sort_by_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;
  const gchar *rname1, *rname2;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;

  rname1 = gst_plugin_feature_get_name (f1);
  rname2 = gst_plugin_feature_get_name (f2);

  diff = strcmp (rname2, rname1);

  return diff;
}

static gpointer
_get_decoder_factories (gpointer arg)
{
  GList *factories;
  GstCaps *desired_caps =
      gst_caps_from_string ("audio/mpeg,mpegversion = (int) 1;"
      "audio/x-private1-lpcm; " "audio/x-private1-ac3; audio/ac3; "
      "audio/x-private1-dts");

  /* Set decoder caps to empty. Will be filled by the factory_filter */
  decoder_caps = gst_caps_new_empty ();

  factories = gst_default_registry_feature_filter (
      (GstPluginFeatureFilter) rsnaudiodec_factory_filter, FALSE, desired_caps);

  decoder_factories = g_list_sort (factories, (GCompareFunc) sort_by_ranks);

  GST_DEBUG ("Available decoder caps %" GST_PTR_FORMAT, decoder_caps);

  return NULL;
}

static GstStateChangeReturn
rsn_audiodec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  RsnAudioDec *self = RSN_AUDIODEC (element);
  static GOnce gonce = G_ONCE_INIT;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstElement *new_child;
      new_child = gst_element_factory_make ("autoconvert", NULL);
      g_once (&gonce, _get_decoder_factories, NULL);
      g_object_set (G_OBJECT (new_child), "factories", decoder_factories, NULL);
      if (new_child == NULL || !rsn_audiodec_set_child (self, new_child))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;
  ret =
      GST_ELEMENT_CLASS (rsn_audiodec_parent_class)->change_state (element,
      transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      cleanup_child (self);
      break;
    default:
      break;
  }

  return ret;

}
