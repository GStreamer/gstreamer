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


static GstElement *compositor_pad_creator = NULL;

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
  g_source_unref (source);
}

void
ges_callback_add (GSourceFunc func, gpointer udata, GDestroyNotify notify)
{
  GMainContext *context = g_main_context_get_thread_default ();
  GSource *source = g_idle_source_new ();
  if (!context)
    context = g_main_context_default ();

  g_source_set_callback (source, func, udata, notify);
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_attach (source, context);
  g_source_unref (source);
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

static GstElement *
get_internal_mixer (GstElementFactory * factory)
{
  GstElement *mixer = NULL, *elem = NULL;

  if (g_type_is_a (gst_element_factory_get_element_type (factory),
          GST_TYPE_AGGREGATOR)) {
    return gst_element_factory_create (factory, NULL);
  }

  if (!g_type_is_a (gst_element_factory_get_element_type (factory),
          GST_TYPE_BIN)) {
    goto done;
  }

  GParamSpec *pspec;

  elem = gst_element_factory_create (factory, NULL);

  /* Checks whether this element has mixer property and the internal element
   * is aggregator subclass */
  if (!elem) {
    goto done;
  }

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (elem), "mixer");
  if (!pspec) {
    goto done;
  }

  if (!g_type_is_a (pspec->value_type, GST_TYPE_ELEMENT)) {
    goto done;
  }

  g_object_get (elem, "mixer", &mixer, NULL);
  if (!mixer) {
    goto done;
  }

  if (!GST_IS_AGGREGATOR (mixer)) {
    gst_clear_object (&mixer);
  }

done:
  gst_clear_object (&elem);

  return mixer;
}

static inline void
_init_value_from_spec_for_serialization (GValue * value, GParamSpec * spec)
{

  if (g_type_is_a (spec->value_type, G_TYPE_ENUM) ||
      g_type_is_a (spec->value_type, G_TYPE_FLAGS))
    g_value_init (value, G_TYPE_INT);
  else
    g_value_init (value, spec->value_type);
}

GstStructure *
ges_util_object_properties_to_structure (GObject * object,
    const gchar ** ignored_fields)
{
  guint n_props, j;
  GParamSpec *spec, **pspecs;
  GObjectClass *class = G_OBJECT_GET_CLASS (object);
  GstStructure *structure = gst_structure_new_empty ("properties");

  pspecs = g_object_class_list_properties (class, &n_props);
  for (j = 0; j < n_props; j++) {
    GValue val = { 0 };


    spec = pspecs[j];
    if (!ges_util_can_serialize_spec (spec))
      continue;

    if (ignored_fields) {
      if (g_strv_contains ((const gchar * const *) ignored_fields, spec->name))
        continue;
    }

    _init_value_from_spec_for_serialization (&val, spec);
    g_object_get_property (object, spec->name, &val);

    /* A NULL structure (e.g. an unset `converter-config`) can't be
     * serialized and is the default value anyway */
    if (G_VALUE_HOLDS (&val, GST_TYPE_STRUCTURE) &&
        gst_value_get_structure (&val) == NULL) {
      GST_LOG ("Ignoring %s as it is NULL", spec->name);
      goto next;
    }

    const GValue *default_val = g_param_spec_get_default_value (spec);
    if (g_type_is_a (spec->value_type, G_TYPE_ENUM) ||
        g_type_is_a (spec->value_type, G_TYPE_FLAGS)) {
      gint default_int = g_type_is_a (spec->value_type,
          G_TYPE_ENUM) ? g_value_get_enum (default_val) :
          g_value_get_flags (default_val);
      if (g_value_get_int (&val) == default_int) {
        GST_LOG ("Ignoring %s as it is using the default value", spec->name);
        goto next;
      }

    } else {
      if (gst_value_compare (default_val, &val) == GST_VALUE_EQUAL) {
        GST_LOG ("Ignoring %s as it is using the default value", spec->name);
        goto next;
      }
    }

    if (spec->value_type == GST_TYPE_CAPS) {
      gchar *caps_str;
      const GstCaps *caps = gst_value_get_caps (&val);

      caps_str = gst_caps_to_string (caps);
      gst_structure_set (structure, spec->name, G_TYPE_STRING, caps_str, NULL);
      g_free (caps_str);
      goto next;
    }

    gst_structure_set_value (structure, spec->name, &val);

  next:
    g_value_unset (&val);
  }
  g_free (pspecs);


  return structure;
}

void
ges_compositor_pad_reset (void)
{
  gst_clear_object (&compositor_pad_creator);
}

GstPad *
ges_compositor_pad_new (void)
{
  if (!compositor_pad_creator) {
    GstElementFactory *factory = NULL;
    GstElement *compositor =
        ges_video_element_selector_make_compositor
        (ges_video_element_selector (), NULL);

    if (!compositor)
      return NULL;

    factory = gst_element_get_factory (compositor);
    if (factory)
      compositor_pad_creator = get_internal_mixer (factory);
    gst_object_unref (compositor);
  }

  if (!compositor_pad_creator)
    return NULL;

  return gst_element_request_pad_simple (compositor_pad_creator, "sink_%u");
}
