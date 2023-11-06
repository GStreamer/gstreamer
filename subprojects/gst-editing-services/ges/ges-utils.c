/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:ges-utils
 * @title: GES utilities
 * @short_description: Convenience methods
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "ges-internal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-layer.h"
#include "ges.h"
#include <gst/base/base.h>

static GstElementFactory *compositor_factory = NULL;

/**
 * ges_timeline_new_audio_video:
 *
 * Creates a new timeline containing a single #GESAudioTrack and a
 * single #GESVideoTrack.
 *
 * Returns: (transfer floating): The new timeline.
 */

GESTimeline *
ges_timeline_new_audio_video (void)
{
  GESTrack *tracka, *trackv;
  GESTimeline *timeline;

  /* This is our main GESTimeline */
  timeline = ges_timeline_new ();

  tracka = GES_TRACK (ges_audio_track_new ());
  trackv = GES_TRACK (ges_video_track_new ());

  if (!ges_timeline_add_track (timeline, trackv) ||
      !ges_timeline_add_track (timeline, tracka)) {
    gst_object_unref (timeline);
    timeline = NULL;
  }

  return timeline;
}

/* Internal utilities */
gint
element_start_compare (GESTimelineElement * a, GESTimelineElement * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    if (a->duration < b->duration)
      return -1;
    if (a->duration > b->duration)
      return 1;
    return 0;
  } else if (a->start < b->start)
    return -1;

  return 1;
}

gint
element_end_compare (GESTimelineElement * a, GESTimelineElement * b)
{
  if (GES_TIMELINE_ELEMENT_END (a) == GES_TIMELINE_ELEMENT_END (b)) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    if (a->duration < b->duration)
      return -1;
    if (a->duration > b->duration)
      return 1;
    return 0;
  } else if (GES_TIMELINE_ELEMENT_END (a) < (GES_TIMELINE_ELEMENT_END (b)))
    return -1;

  return 1;
}

gboolean
ges_pspec_equal (gconstpointer key_spec_1, gconstpointer key_spec_2)
{
  const GParamSpec *key1 = key_spec_1;
  const GParamSpec *key2 = key_spec_2;

  return (key1->owner_type == key2->owner_type &&
      strcmp (key1->name, key2->name) == 0);
}

guint
ges_pspec_hash (gconstpointer key_spec)
{
  const GParamSpec *key = key_spec;
  const gchar *p;
  guint h = key->owner_type;

  for (p = key->name; *p; p++)
    h = (h << 5) - h + *p;

  return h;
}

static gboolean
find_compositor (GstPluginFeature * feature, gpointer udata)
{
  gboolean res = FALSE;
  const gchar *klass;
  GstPluginFeature *loaded_feature = NULL;
  GstElement *elem = NULL;

  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);

  if (strstr (klass, "Compositor") == NULL)
    return FALSE;

  loaded_feature = gst_plugin_feature_load (feature);
  if (!loaded_feature) {
    GST_ERROR ("Could not load feature: %" GST_PTR_FORMAT, feature);
    return FALSE;
  }

  /* glvideomixer consists of bin with internal mixer element */
  if (g_type_is_a (gst_element_factory_get_element_type (GST_ELEMENT_FACTORY
              (loaded_feature)), GST_TYPE_BIN)) {
    GParamSpec *pspec;
    GstElement *mixer = NULL;

    elem =
        gst_element_factory_create (GST_ELEMENT_FACTORY_CAST (loaded_feature),
        NULL);

    /* Checks whether this element has mixer property and the internal element
     * is aggregator subclass */
    if (!elem) {
      GST_ERROR ("Could not create element from factory %" GST_PTR_FORMAT,
          feature);
      goto done;
    }

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (elem), "mixer");
    if (!pspec)
      goto done;

    if (!g_type_is_a (pspec->value_type, GST_TYPE_ELEMENT))
      goto done;

    g_object_get (elem, "mixer", &mixer, NULL);
    if (!mixer)
      goto done;

    if (GST_IS_AGGREGATOR (mixer))
      res = TRUE;

    gst_object_unref (mixer);
  } else {
    res =
        g_type_is_a (gst_element_factory_get_element_type (GST_ELEMENT_FACTORY
            (loaded_feature)), GST_TYPE_AGGREGATOR);
  }

done:
  gst_clear_object (&elem);
  gst_object_unref (loaded_feature);
  return res;
}

gboolean
ges_util_structure_get_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime * val, GESFrameNumber * frames)
{
  gboolean found = FALSE;

  const GValue *gvalue;

  if (!val && !frames)
    return FALSE;

  gvalue = gst_structure_get_value (structure, name);
  if (!gvalue)
    return FALSE;

  if (frames)
    *frames = GES_FRAME_NUMBER_NONE;

  found = TRUE;
  if (val && G_VALUE_TYPE (gvalue) == GST_TYPE_CLOCK_TIME) {
    *val = (GstClockTime) g_value_get_uint64 (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_UINT64) {
    *val = (GstClockTime) g_value_get_uint64 (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_UINT) {
    *val = (GstClockTime) g_value_get_uint (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_INT) {
    *val = (GstClockTime) g_value_get_int (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_INT64) {
    *val = (GstClockTime) g_value_get_int64 (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_DOUBLE) {
    gdouble d = g_value_get_double (gvalue);

    if (d == -1.0)
      *val = GST_CLOCK_TIME_NONE;
    else
      *val = d * GST_SECOND;
  } else if (frames && G_VALUE_TYPE (gvalue) == G_TYPE_STRING) {
    const gchar *str = g_value_get_string (gvalue);

    found = FALSE;
    if (str && str[0] == 'f') {
      GValue v = G_VALUE_INIT;

      g_value_init (&v, G_TYPE_UINT64);
      if (gst_value_deserialize (&v, &str[1])) {
        *frames = g_value_get_uint64 (&v);
        if (val)
          *val = GST_CLOCK_TIME_NONE;
        found = TRUE;
      }
      g_value_reset (&v);
    }
  } else {
    found = FALSE;

  }

  return found;
}


GstElementFactory *
ges_get_compositor_factory (void)
{
  GList *result;

  if (compositor_factory)
    return compositor_factory;

  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) find_compositor, FALSE, NULL);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);
  g_assert (result);

  compositor_factory = result->data;
  gst_plugin_feature_list_free (result);

  return compositor_factory;
}

void
ges_timeout_add (guint interval, GSourceFunc func, gpointer udata,
    GDestroyNotify notify)
{
  GMainContext *context = g_main_context_get_thread_default ();
  GSource *source = g_timeout_source_new (interval);

  if (!context)
    context = g_main_context_default ();

  g_source_set_callback (source, func, udata, notify);
  g_source_attach (source, context);
}

void
ges_idle_add (GSourceFunc func, gpointer udata, GDestroyNotify notify)
{
  GMainContext *context = g_main_context_get_thread_default ();
  GSource *source = g_idle_source_new ();
  if (!context)
    context = g_main_context_default ();

  g_source_set_callback (source, func, udata, notify);
  g_source_attach (source, context);

}

gboolean
ges_nle_composition_add_object (GstElement * comp, GstElement * object)
{
  return gst_bin_add (GST_BIN (comp), object);
}

gboolean
ges_nle_composition_remove_object (GstElement * comp, GstElement * object)
{
  return gst_bin_remove (GST_BIN (comp), object);
}

gboolean
ges_nle_object_commit (GstElement * nlesource, gboolean recurse)
{
  gboolean ret;

  g_signal_emit_by_name (nlesource, "commit", recurse, &ret);

  return ret;
}
