/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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
#include <string.h>

#include "gstfactorylists.h"

typedef struct
{
  GstFactoryListType type;
} FilterData;

/* function used to sort element features. We first sort on the rank, then
 * on the element name (to get a consistent, predictable list) */
static gint
compare_ranks (GValue * v1, GValue * v2)
{
  gint diff;
  GstPluginFeature *f1, *f2;

  f1 = g_value_get_object (v1);
  f2 = g_value_get_object (v2);

  diff = f2->rank - f1->rank;
  if (diff != 0)
    return diff;

  diff = strcmp (f2->name, f1->name);

  return diff;
}

/* the filter function for selecting the elements we can use in
 * autoplugging */
static gboolean
decoders_filter (GstElementFactory * factory)
{
  guint rank;
  const gchar *klass;

  klass = gst_element_factory_get_klass (factory);
  /* only demuxers, decoders, depayloaders and parsers can play */
  if (strstr (klass, "Demux") == NULL &&
      strstr (klass, "Decoder") == NULL &&
      strstr (klass, "Depayloader") == NULL &&
      strstr (klass, "Parse") == NULL) {
    return FALSE;
  }

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory));
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

/* the filter function for selecting the elements we can use in
 * autoplugging */
static gboolean
sinks_filter (GstElementFactory * factory)
{
  guint rank;
  const gchar *klass;

  klass = gst_element_factory_get_klass (factory);
  /* only sinks can play */
  if (strstr (klass, "Sink") == NULL) {
    return FALSE;
  }

  /* must be audio or video sink */
  if (strstr (klass, "Audio") == NULL && strstr (klass, "Video") == NULL) {
    return FALSE;
  }

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory));
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

/**
 * gst_factory_list_is_type:
 * @factory: a #GstElementFactory
 * @type: a #GstFactoryListType
 *
 * Check if @factory if of the given types.
 *
 * Returns: %TRUE if @factory is of @type.
 */
gboolean
gst_factory_list_is_type (GstElementFactory * factory, GstFactoryListType type)
{
  gboolean res = FALSE;

  if (!res && (type & GST_FACTORY_LIST_SINK))
    res = sinks_filter (factory);
  if (!res && (type & GST_FACTORY_LIST_DECODER))
    res = decoders_filter (factory);

  return res;
}

static gboolean
element_filter (GstPluginFeature * feature, FilterData * data)
{
  gboolean res;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  res =
      gst_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (feature), data->type);

  return res;
}

/**
 * gst_factory_list_get_elements:
 * @type: a #GstFactoryListType
 *
 * Get a sorted list of factories of @type.
 *
 * Returns: a #GValueArray of #GstElementFactory elements. Use
 * g_value_array_free() after usage.
 */
GValueArray *
gst_factory_list_get_elements (GstFactoryListType type)
{
  GValueArray *result;
  GList *walk, *list;
  FilterData data;

  result = g_value_array_new (0);

  /* prepare type */
  data.type = type;

  /* get the feature list using the filter */
  list = gst_default_registry_feature_filter ((GstPluginFeatureFilter)
      element_filter, FALSE, &data);

  /* convert to an array */
  for (walk = list; walk; walk = g_list_next (walk)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY_CAST (walk->data);
    GValue val = { 0, };

    g_value_init (&val, G_TYPE_OBJECT);
    g_value_set_object (&val, factory);
    g_value_array_append (result, &val);
    g_value_unset (&val);
  }
  gst_plugin_feature_list_free (list);

  /* sort on rank and name */
  g_value_array_sort (result, (GCompareFunc) compare_ranks);

  return result;
}

/**
 * gst_factory_list_debug:
 * @array: an array of element factories
 *
 * Debug the element factory names in @array.
 */
void
gst_factory_list_debug (GValueArray * array)
{
#ifndef GST_DISABLE_GST_DEBUG
  gint i;

  for (i = 0; i < array->n_values; i++) {
    GValue *value;
    GstPluginFeature *feature;

    value = g_value_array_get_nth (array, i);
    feature = g_value_get_object (value);

    GST_DEBUG ("%s", gst_plugin_feature_get_name (feature));
  }
#endif
}

/**
 * gst_factory_list_filter:
 * @array: a #GValueArray to filter
 * @caps: a #GstCaps
 *
 * Filter out all the elementfactories in @array that can handle @caps as
 * input.
 *
 * Returns: a #GValueArray of #GstElementFactory elements. Use
 * g_value_array_free() after usage.
 */
GValueArray *
gst_factory_list_filter (GValueArray * array, const GstCaps * caps)
{
  GValueArray *result;
  gint i;

  result = g_value_array_new (0);

  GST_DEBUG ("finding factories");

  /* loop over all the factories */
  for (i = 0; i < array->n_values; i++) {
    GValue *value;
    GstElementFactory *factory;
    const GList *templates;
    GList *walk;

    value = g_value_array_get_nth (array, i);
    factory = g_value_get_object (value);

    /* get the templates from the element factory */
    templates = gst_element_factory_get_static_pad_templates (factory);
    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstStaticPadTemplate *templ = walk->data;

      /* we only care about the sink templates */
      if (templ->direction == GST_PAD_SINK) {
        GstCaps *tmpl_caps;

        /* try to intersect the caps with the caps of the template */
        tmpl_caps = gst_static_caps_get (&templ->static_caps);

        /* FIXME, intersect is not the right method, we ideally want to check
         * for a subset here */

        /* check if the intersection is empty */
        if (gst_caps_can_intersect (caps, tmpl_caps)) {
          /* non empty intersection, we can use this element */
          GValue resval = { 0, };
          g_value_init (&resval, G_TYPE_OBJECT);
          g_value_set_object (&resval, factory);
          g_value_array_append (result, &resval);
          g_value_unset (&resval);
          gst_caps_unref (tmpl_caps);
          break;
        }
        gst_caps_unref (tmpl_caps);
      }
    }
  }
  return result;
}
