/* GStreamer Editing Services
 *
 * Copyright (C) 2026 Igalia
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/base/base.h>
#include <gst/video/video.h>

#include "ges-internal.h"
#include "gstframepositioner.h"

/* Cheap klass-only filter - no plugin load, so safe to run on every
 * factory at startup. */
static gboolean
find_compositor_klass (GstPluginFeature * feature, gpointer udata)
{
  const gchar *klass;

  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);

  return klass && strstr (klass, "Compositor") != NULL;
}

static gboolean
find_compositor (GstPluginFeature * feature, gpointer udata)
{
  gboolean res = FALSE;
  GstPluginFeature *loaded_feature = NULL;
  GstElement *elem = NULL;

  if (!find_compositor_klass (feature, udata))
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

  if (res) {
    const gchar *needed_props[] = { "width", "height", "xpos", "ypos" };
    GObjectClass *klass =
        g_type_class_ref (gst_element_factory_get_element_type
        (GST_ELEMENT_FACTORY (loaded_feature)));
    GstPadTemplate *templ =
        gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass),
        "sink_%u");

    g_type_class_unref (klass);
    if (!templ) {
      GST_INFO_OBJECT (loaded_feature, "No sink template found, ignoring");
      res = FALSE;
      goto done;
    }

    GType pad_type;
    g_object_get (templ, "gtype", &pad_type, NULL);
    klass = g_type_class_ref (pad_type);
    for (gint i = 0; i < G_N_ELEMENTS (needed_props); i++) {
      GParamSpec *pspec;

      if (!(pspec = g_object_class_find_property (klass, needed_props[i]))) {
        GST_INFO_OBJECT (loaded_feature, "No property %s found, ignoring",
            needed_props[i]);
        res = FALSE;
        break;
      }

      if (pspec->value_type != G_TYPE_INT && pspec->value_type != G_TYPE_FLOAT
          && pspec->value_type != G_TYPE_DOUBLE) {
        GST_INFO_OBJECT (loaded_feature,
            "Property %s is not of type int or float, or double, ignoring",
            needed_props[i]);
        res = FALSE;
        break;
      }
    }
    g_type_class_unref (klass);
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


/* Roles whose factory is a system-memory element standing in for a
 * missing native one. The corresponding maker wraps it with
 * `downloader ! core ! uploader` so the core runs in sysmem while the
 * rest of the chain stays native. */
typedef enum
{
  GES_SELECTOR_FALLBACK_COLORCONVERT = 1 << 0,
  GES_SELECTOR_FALLBACK_SCALE = 1 << 1,
  GES_SELECTOR_FALLBACK_VIDEOFLIP = 1 << 2,
  GES_SELECTOR_FALLBACK_DEINTERLACE = 1 << 3,
} GESVideoElementSelectorFallbackFlags;

struct _GESVideoElementSelector
{
  GstElementFactory *compositor;
  gboolean strict;
  const gchar *memory_feature;
  GstElementFactory *uploader;
  GstElementFactory *downloader;
  GstElementFactory *colorconvert;
  GstElementFactory *convert_scale;
  GstElementFactory *scale;
  GstElementFactory *videoflip;
  GstElementFactory *deinterlace;
  GESVideoElementSelectorFallbackFlags sw_fallback_mask;
};

gboolean
ges_video_element_selector_is_strict (const GESVideoElementSelector * self)
{
  g_return_val_if_fail (self != NULL, FALSE);
  return self->strict;
}

GstElement *
ges_video_element_selector_make_compositor (const GESVideoElementSelector *
    self, const gchar * name)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->compositor != NULL, NULL);
  return gst_element_factory_create (self->compositor, name);
}

GstElement *
ges_video_element_selector_make_colorconvert_bare (const GESVideoElementSelector
    * self, const gchar * name)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->colorconvert != NULL, NULL);
  return gst_element_factory_create (self->colorconvert, name);
}

static GstElementFactory *
find_best_compositor_factory (void)
{
  GList *result;
  GstElementFactory *factory;

  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) find_compositor, FALSE, NULL);
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);
  g_assert (result);

  factory = gst_object_ref (result->data);
  gst_plugin_feature_list_free (result);

  return factory;
}

typedef struct
{
  /* `/`-separated klass keywords; each must be a substring of klass. */
  const gchar *required_classifications;
  /* `/`-separated klass keywords that must not appear in klass. Keeps
   * out supersets whose extra capabilities carry different defaults
   * (e.g. `videoconvertscale` as a stand-in for `videoconvert` would
   * silently flip `add-borders` behaviour). */
  const gchar *excluded_classifications;
  /* Memory-feature constraints. %NULL means system memory. */
  const gchar *sink_memory;
  const gchar *src_memory;
  /* Interface the element must implement, or %G_TYPE_NONE. */
  GType required_interface;
  /* Prefer factories shipped in this plugin as a tiebreaker. */
  GstPlugin *preferred_plugin;
  /* When %TRUE, `video/x-raw(ANY)` pad caps don't match a specific
   * memory feature. */
  gboolean reject_any_on_memory_match;
} FactoryQuery;

/* Exactly one ALWAYS sink and one ALWAYS src pad template. */
static gboolean
factory_is_plain_filter (GstElementFactory * factory)
{
  const GList *templs = gst_element_factory_get_static_pad_templates (factory);
  guint sink = 0, src = 0;

  for (const GList * l = templs; l; l = l->next) {
    GstStaticPadTemplate *st = l->data;
    if (st->presence != GST_PAD_ALWAYS)
      return FALSE;
    if (st->direction == GST_PAD_SINK)
      sink++;
    else if (st->direction == GST_PAD_SRC)
      src++;
  }
  return sink == 1 && src == 1;
}

static gboolean
pad_supports_memory_type (GstElementFactory * factory,
    GstPadDirection direction, const gchar * memory, gboolean reject_any)
{
  const GList *templs = gst_element_factory_get_static_pad_templates (factory);
  gboolean matched = FALSE;

  for (const GList * l = templs; l && !matched; l = l->next) {
    GstStaticPadTemplate *st = l->data;
    GstCaps *caps;

    if (st->direction != direction)
      continue;

    caps = gst_static_caps_get (&st->static_caps);
    if (!caps)
      continue;

    for (guint i = 0; i < gst_caps_get_size (caps); i++) {
      GstStructure *s = gst_caps_get_structure (caps, i);
      GstCapsFeatures *features = gst_caps_get_features (caps, i);

      if (g_strcmp0 (gst_structure_get_name (s), "video/x-raw") != 0)
        continue;

      if (memory == NULL) {
        /* Sysmem: require explicit handling (null or SystemMemory-only
         * features). `ANY` is too weak. */
        if (features && gst_caps_features_is_any (features))
          continue;
        if (!features) {
          matched = TRUE;
          break;
        }
        guint n = gst_caps_features_get_size (features);
        gboolean only_sysmem = TRUE;
        for (guint j = 0; j < n; j++) {
          const gchar *feat = gst_caps_features_get_nth (features, j);
          if (g_str_has_prefix (feat, "memory:") &&
              !g_str_equal (feat, "memory:SystemMemory")) {
            only_sysmem = FALSE;
            break;
          }
        }
        if (only_sysmem) {
          matched = TRUE;
          break;
        }
      } else if (features) {
        /* Non-sysmem target: explicit @memory, or `ANY` (adaptive
         * uploaders use it) unless @reject_any. */
        if (gst_caps_features_is_any (features)) {
          if (reject_any)
            continue;
          matched = TRUE;
          break;
        }
        if (gst_caps_features_contains (features, memory)) {
          matched = TRUE;
          break;
        }
      }
    }
    gst_caps_unref (caps);
  }
  return matched;
}

static gboolean
query_matches_factory (GstPluginFeature * feature, gpointer data)
{
  const FactoryQuery *q = data;
  GstElementFactory *factory;
  const gchar *klass;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  factory = GST_ELEMENT_FACTORY_CAST (feature);
  if (!factory_is_plain_filter (factory))
    return FALSE;
  klass = gst_element_factory_get_metadata (factory,
      GST_ELEMENT_METADATA_KLASS);
  if (!klass)
    return FALSE;

  if (q->required_classifications) {
    gchar **segments = g_strsplit (q->required_classifications, "/", 0);
    for (gchar ** s = segments; *s; s++) {
      if (**s && !strstr (klass, *s)) {
        g_strfreev (segments);
        return FALSE;
      }
    }
    g_strfreev (segments);
  }
  if (q->excluded_classifications) {
    gchar **segments = g_strsplit (q->excluded_classifications, "/", 0);
    for (gchar ** s = segments; *s; s++) {
      if (**s && strstr (klass, *s)) {
        g_strfreev (segments);
        return FALSE;
      }
    }
    g_strfreev (segments);
  }

  if (!pad_supports_memory_type (factory, GST_PAD_SINK, q->sink_memory,
          q->reject_any_on_memory_match))
    return FALSE;
  if (!pad_supports_memory_type (factory, GST_PAD_SRC, q->src_memory,
          q->reject_any_on_memory_match))
    return FALSE;

  return TRUE;
}

static gint
compare_preferring_plugin (gconstpointer a, gconstpointer b, gpointer udata)
{
  GstPlugin *preferred = udata;
  gboolean a_pref = FALSE, b_pref = FALSE;

  if (preferred) {
    GstPlugin *plugin_a =
        gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (a));
    GstPlugin *plugin_b =
        gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (b));
    a_pref = plugin_a == preferred;
    b_pref = plugin_b == preferred;
    gst_clear_object (&plugin_a);
    gst_clear_object (&plugin_b);
  }

  if (a_pref != b_pref)
    return a_pref ? -1 : 1;
  return gst_plugin_feature_rank_compare_func (a, b);
}

/* Walk the registry, keep factories matching the query's klass + pad
 * template + (optional) interface constraints, rank-order preferring
 * the query's preferred plugin, return the first. */
static GstElementFactory *
find_factory (const FactoryQuery * q)
{
  GList *candidates;
  GstElementFactory *chosen = NULL;
  const gchar *interface_name = q->required_interface != G_TYPE_NONE ?
      g_type_name (q->required_interface) : NULL;

  candidates = gst_registry_feature_filter (gst_registry_get (),
      query_matches_factory, FALSE, (gpointer) q);
  candidates = g_list_sort_with_data (candidates, compare_preferring_plugin,
      (gpointer) q->preferred_plugin);

  for (GList * l = candidates; l && !chosen; l = l->next) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY_CAST (l->data);

    if (interface_name &&
        !gst_element_factory_has_interface (factory, interface_name))
      continue;
    chosen = gst_object_ref (factory);
  }
  gst_plugin_feature_list_free (candidates);
  return chosen;
}

/* If *slot is NULL and @memory_feature is set, run @query again with
 * sysmem on both pads and set @fallback_bit on success. Leaves the
 * query's memory constraints restored to @memory_feature. */
static void
try_sysmem_fallback (GESVideoElementSelector * self, GstElementFactory ** slot,
    FactoryQuery * query, const gchar * memory_feature,
    GESVideoElementSelectorFallbackFlags fallback_bit, const gchar * role)
{
  if (*slot || !memory_feature)
    return;
  query->sink_memory = NULL;
  query->src_memory = NULL;
  *slot = find_factory (query);
  if (*slot) {
    self->sw_fallback_mask |= fallback_bit;
    GST_WARNING ("Backend '%s' has no native %s; falling back to sysmem "
        "`%s` wrapped with downloader/uploader. Expect memory round-trips.",
        memory_feature, role, GST_OBJECT_NAME (*slot));
  }
  query->sink_memory = memory_feature;
  query->src_memory = memory_feature;
}

/* Resolve each role by querying the registry. */
static gboolean
populate_selector (GESVideoElementSelector * self, const gchar * memory_feature)
{
  FactoryQuery query = { 0, };

  query.required_interface = G_TYPE_NONE;
  query.preferred_plugin = self->compositor ?
      gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (self->compositor))
      : NULL;
  query.sink_memory = memory_feature;
  query.src_memory = memory_feature;
  /* Reject `video/x-raw(ANY)` templates: deinterlace / videoflip / ...
   * declare one for passthrough of a subset of inputs, not for real
   * processing of non-sysmem memory. Uploader/downloader clear this. */
  query.reject_any_on_memory_match = TRUE;

  query.required_classifications = "Converter/Video/Colorspace";
  query.excluded_classifications = "Scaler";
  self->colorconvert = find_factory (&query);
  try_sysmem_fallback (self, &self->colorconvert, &query, memory_feature,
      GES_SELECTOR_FALLBACK_COLORCONVERT, "colorconvert");

  query.required_classifications = "Converter/Video/Scaler/Colorspace";
  query.excluded_classifications = NULL;
  self->convert_scale = find_factory (&query);

  query.required_classifications = "Video/Scaler";
  query.excluded_classifications = "Colorspace";
  self->scale = find_factory (&query);
  try_sysmem_fallback (self, &self->scale, &query, memory_feature,
      GES_SELECTOR_FALLBACK_SCALE, "scale");

  query.required_classifications = "Video/Deinterlace";
  query.excluded_classifications = NULL;
  self->deinterlace = find_factory (&query);
  try_sysmem_fallback (self, &self->deinterlace, &query, memory_feature,
      GES_SELECTOR_FALLBACK_DEINTERLACE, "deinterlace");

  /* Combined converters (d3d11convert, cudaconvertscale, ...) also
   * implement GstVideoDirection, exclude them to keep the dedicated
   * flip. */
  query.required_classifications = NULL;
  query.excluded_classifications = "Converter/Scaler";
  query.required_interface = GST_TYPE_VIDEO_DIRECTION;
  self->videoflip = find_factory (&query);
  try_sysmem_fallback (self, &self->videoflip, &query, memory_feature,
      GES_SELECTOR_FALLBACK_VIDEOFLIP, "videoflip");
  query.required_interface = G_TYPE_NONE;

  if (memory_feature) {
    query.reject_any_on_memory_match = FALSE;
    query.excluded_classifications = NULL;
    query.sink_memory = NULL;
    query.src_memory = memory_feature;
    query.required_classifications = "Uploader/Video";
    self->uploader = find_factory (&query);

    query.sink_memory = memory_feature;
    query.src_memory = NULL;
    query.required_classifications = "Downloader/Video";
    self->downloader = find_factory (&query);
  }

  gst_clear_object (&query.preferred_plugin);

  /* Any role using a sysmem fallback needs both uploader and
   * downloader to wrap the core in strict mode. If we needed a
   * fallback but the backend ships no uploader/downloader pair the
   * backend is unusable; fail hard rather than silently running the
   * wrong memory on the chain. */
  if (self->sw_fallback_mask &&
      memory_feature && (!self->uploader || !self->downloader)) {
    GST_WARNING ("Backend '%s' needs a sysmem fallback for some video "
        "roles but ships no uploader/downloader - refusing to use it.",
        memory_feature);
    gst_clear_object (&self->colorconvert);
    gst_clear_object (&self->convert_scale);
    gst_clear_object (&self->scale);
    gst_clear_object (&self->videoflip);
    gst_clear_object (&self->deinterlace);
    gst_clear_object (&self->uploader);
    gst_clear_object (&self->downloader);
    self->sw_fallback_mask = 0;
    return FALSE;
  }

  if (!self->colorconvert || !self->deinterlace || !self->videoflip ||
      (!self->convert_scale && !self->scale) ||
      (memory_feature && (!self->uploader || !self->downloader))) {
    GST_DEBUG ("Memory feature '%s' resolved incompletely, skipping",
        memory_feature ? memory_feature : "(sysmem)");
    gst_clear_object (&self->colorconvert);
    gst_clear_object (&self->convert_scale);
    gst_clear_object (&self->scale);
    gst_clear_object (&self->videoflip);
    gst_clear_object (&self->deinterlace);
    gst_clear_object (&self->uploader);
    gst_clear_object (&self->downloader);
    self->sw_fallback_mask = 0;
    return FALSE;
  }

  /* Intern so the singleton holds a stable pointer. */
  self->memory_feature =
      memory_feature ? g_intern_string (memory_feature) : NULL;

  GST_DEBUG ("Backend '%s' resolved (sw_fallback=0x%x): cc=%s cs=%s sc=%s "
      "flip=%s di=%s up=%s down=%s",
      self->memory_feature ? self->memory_feature : "software",
      self->sw_fallback_mask,
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->colorconvert)),
      self->convert_scale ? gst_plugin_feature_get_name (GST_PLUGIN_FEATURE
          (self->convert_scale)) : "(none)",
      self->scale ? gst_plugin_feature_get_name (GST_PLUGIN_FEATURE
          (self->scale)) : "(none)",
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->videoflip)),
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->deinterlace)),
      self->uploader ? gst_plugin_feature_get_name (GST_PLUGIN_FEATURE
          (self->uploader)) : "(none)",
      self->downloader ? gst_plugin_feature_get_name (GST_PLUGIN_FEATURE
          (self->downloader)) : "(none)");
  return TRUE;
}

/* Walk the compositor sink caps, try each advertised memory feature
 * until one resolves. @strict means the compositor doesn't accept
 * sysmem - the whole source chain must stay native. */
static void
resolve_video_element_selector (GESVideoElementSelector * self)
{
  gboolean has_sysmem = FALSE;
  GType elem_type;
  GstElementClass *klass;
  GstPadTemplate *tmpl;
  GstCaps *sink_caps;
  GPtrArray *memory_features;
  gboolean matched = FALSE;

  self->compositor = find_best_compositor_factory ();
  self->strict = FALSE;

  if (!self->compositor)
    goto fallback;

  elem_type = gst_element_factory_get_element_type (self->compositor);
  klass = (GstElementClass *) g_type_class_ref (elem_type);
  tmpl = gst_element_class_get_pad_template (klass, "sink_%u");
  sink_caps = tmpl ? gst_pad_template_get_caps (tmpl) : NULL;
  g_type_class_unref (klass);

  if (!sink_caps)
    goto fallback;

  memory_features = g_ptr_array_new_with_free_func (g_free);
  for (guint i = 0; i < gst_caps_get_size (sink_caps); i++) {
    GstCapsFeatures *f = gst_caps_get_features (sink_caps, i);
    guint n;

    if (f && gst_caps_features_is_any (f)) {
      has_sysmem = TRUE;
      continue;
    }

    n = f ? gst_caps_features_get_size (f) : 0;
    gboolean has_memory = FALSE;
    for (guint j = 0; j < n; j++) {
      const gchar *feat = gst_caps_features_get_nth (f, j);
      if (!g_str_has_prefix (feat, "memory:"))
        continue;
      has_memory = TRUE;
      if (g_str_equal (feat, "memory:SystemMemory")) {
        has_sysmem = TRUE;
        continue;
      }
      gboolean dup = FALSE;
      for (guint k = 0; k < memory_features->len; k++)
        if (g_str_equal (memory_features->pdata[k], feat)) {
          dup = TRUE;
          break;
        }
      if (!dup)
        g_ptr_array_add (memory_features, g_strdup (feat));
    }
    if (!has_memory)
      has_sysmem = TRUE;
  }
  gst_caps_unref (sink_caps);

  for (guint i = 0; i < memory_features->len; i++) {
    const gchar *mem = memory_features->pdata[i];
    if (populate_selector (self, mem)) {
      matched = TRUE;
      break;
    }
  }
  g_ptr_array_unref (memory_features);

  if (matched) {
    /* Strict mode (no downloader at the tail of each role) assumes
     * every role has a native factory. Any sysmem fallback in the
     * chain needs sysmem between roles, so drop strict in that case. */
    self->strict = !has_sysmem && !self->sw_fallback_mask;
    goto done;
  }

fallback:
  populate_selector (self, NULL);
done:
  GST_DEBUG ("Compositor '%s': backend=%s strict=%d",
      self->compositor ?
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->compositor)) :
      "(none)",
      self->memory_feature ? self->memory_feature : "software", self->strict);
}

static GESVideoElementSelector cached;
static gboolean cache_initialized = FALSE;
static gboolean rank_signals_connected = FALSE;
static GList *watched_compositor_features = NULL;
G_LOCK_DEFINE_STATIC (cache);

/* Runs with G_LOCK(cache) held. Releases every factory and clears the
 * singleton fields. Does NOT disconnect `notify::rank` handlers -
 * callers remain interested in future rank changes for the lifetime of
 * the process (until ges_deinit). */
static void
selector_invalidate_locked (void)
{
  if (!cache_initialized)
    return;

  gst_clear_object (&cached.compositor);
  gst_clear_object (&cached.uploader);
  gst_clear_object (&cached.downloader);
  gst_clear_object (&cached.colorconvert);
  gst_clear_object (&cached.convert_scale);
  gst_clear_object (&cached.scale);
  gst_clear_object (&cached.videoflip);
  gst_clear_object (&cached.deinterlace);
  memset (&cached, 0, sizeof (cached));
  cache_initialized = FALSE;
}

static void
compositor_rank_changed_cb (GstPluginFeature * feature, GParamSpec * pspec,
    gpointer user_data)
{
  GST_DEBUG ("Compositor rank changed for %s, invalidating selector",
      gst_plugin_feature_get_name (feature));

  G_LOCK (cache);
  selector_invalidate_locked ();
  G_UNLOCK (cache);

  /* The FramePositioner's `operator` enum type is derived from whatever
   * compositor the selector picked - invalidate it in lockstep so the
   * next selector resolve re-queries. */
  gst_compositor_operator_reset_cache ();
}

/* Must run with G_LOCK(cache) held. Idempotent (connects once). */
static void
connect_compositor_rank_signals_locked (void)
{
  GList *result, *l;

  if (rank_signals_connected)
    return;

  /* Cheap klass-only scan - we want every Compositor-klass factory
   * (even ones find_compositor would reject) so any rank bump triggers
   * invalidation, not just bumps of factories that already passed the
   * aggregator check. */
  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) find_compositor_klass, FALSE, NULL);
  for (l = result; l; l = l->next) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (l->data);
    g_signal_connect (feature, "notify::rank",
        G_CALLBACK (compositor_rank_changed_cb), NULL);
    /* Keep a ref so the feature outlives any registry churn and so we
     * have an anchor to disconnect from at deinit. */
    watched_compositor_features =
        g_list_prepend (watched_compositor_features, gst_object_ref (feature));
  }
  gst_plugin_feature_list_free (result);
  rank_signals_connected = TRUE;
}

const GESVideoElementSelector *
ges_video_element_selector (void)
{
  G_LOCK (cache);
  connect_compositor_rank_signals_locked ();
  if (!cache_initialized) {
    resolve_video_element_selector (&cached);
    cache_initialized = TRUE;
  }
  G_UNLOCK (cache);
  return &cached;
}

void
ges_video_element_selector_deinit (void)
{
  GList *l;

  G_LOCK (cache);
  selector_invalidate_locked ();
  for (l = watched_compositor_features; l; l = l->next) {
    g_signal_handlers_disconnect_by_func (l->data,
        (gpointer) compositor_rank_changed_cb, NULL);
  }
  g_list_free_full (watched_compositor_features, gst_object_unref);
  watched_compositor_features = NULL;
  rank_signals_connected = FALSE;
  G_UNLOCK (cache);
}

/* Wrap `core` with the selector's uploader/downloader. The trivial
 * case (software: no uploader, no downloader) returns the bare
 * element; otherwise builds a ghost-padded bin. When @name is
 * non-%NULL, it's used to name the returned top-level element (the
 * wrapper bin when one exists, or the core otherwise); useful for
 * pipeline debug dumps. When @main_element is non-%NULL, it receives a
 * pointer (non-owned) to the core element so callers can configure it
 * directly. */
static GstElement *
make_wrapped_element (const GESVideoElementSelector * self,
    GstElementFactory * core, const gchar * name, GstElement ** main_element)
{
  GstElement *bin, *uploader, *core_elem, *downloader;
  GstPad *sink_pad, *src_pad;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (core != NULL, NULL);

  if (!self->uploader) {
    core_elem = gst_element_factory_create (core, name);
    if (main_element)
      *main_element = core_elem;
    return core_elem;
  }

  bin = gst_bin_new (name);
  uploader = gst_element_factory_create (self->uploader, NULL);
  core_elem = gst_element_factory_create (core, NULL);
  if (main_element)
    *main_element = core_elem;
  gst_bin_add_many (GST_BIN (bin), uploader, core_elem, NULL);
  gst_element_link (uploader, core_elem);

  sink_pad = gst_element_get_static_pad (uploader, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", sink_pad));
  gst_object_unref (sink_pad);

  if (self->downloader && !self->strict) {
    downloader = gst_element_factory_create (self->downloader, NULL);
    gst_bin_add (GST_BIN (bin), downloader);
    gst_element_link (core_elem, downloader);
    src_pad = gst_element_get_static_pad (downloader, "src");
  } else {
    src_pad = gst_element_get_static_pad (core_elem, "src");
  }
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", src_pad));
  gst_object_unref (src_pad);

  return bin;
}

/* Wrap a sysmem core with `downloader ! core ! uploader` so the rest
 * of the native chain can still consume/produce the backend's memory.
 * In non-strict mode the chain is already sysmem - no wrap needed. */
static GstElement *
make_sysmem_fallback_wrapped (const GESVideoElementSelector * self,
    GstElementFactory * sw_core, const gchar * name, GstElement ** main_element)
{
  GstElement *bin, *downloader, *core, *uploader;
  GstPad *sink_pad, *src_pad;

  g_assert (self->uploader && self->downloader);

  if (!self->strict) {
    core = gst_element_factory_create (sw_core, name);
    if (main_element)
      *main_element = core;
    return core;
  }

  bin = gst_bin_new (name);
  downloader = gst_element_factory_create (self->downloader, NULL);
  core = gst_element_factory_create (sw_core, NULL);
  uploader = gst_element_factory_create (self->uploader, NULL);
  if (main_element)
    *main_element = core;
  gst_bin_add_many (GST_BIN (bin), downloader, core, uploader, NULL);
  gst_element_link_many (downloader, core, uploader, NULL);

  sink_pad = gst_element_get_static_pad (downloader, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", sink_pad));
  gst_object_unref (sink_pad);
  src_pad = gst_element_get_static_pad (uploader, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", src_pad));
  gst_object_unref (src_pad);
  return bin;
}

static GstElement *
make_role (const GESVideoElementSelector * self, GstElementFactory * core,
    GESVideoElementSelectorFallbackFlags fallback_bit,
    const gchar * name, GstElement ** main_element)
{
  if (self->sw_fallback_mask & fallback_bit)
    return make_sysmem_fallback_wrapped (self, core, name, main_element);
  return make_wrapped_element (self, core, name, main_element);
}

GstElement *
ges_video_element_selector_make_colorconvert (const GESVideoElementSelector *
    self, const gchar * name, GstElement ** main_element)
{
  return make_role (self, self->colorconvert,
      GES_SELECTOR_FALLBACK_COLORCONVERT, name, main_element);
}

/* Build a bin wrapping `colorconvert ! scale ! colorconvert` with the
 * selector's uploader/downloader, used when the backend has no
 * combined convert_scale element. The two colorconverts guarantee the
 * scaler always sees a format it accepts regardless of the negotiated
 * input caps. Returns the scaler via @main_element. */
static GstElement *
make_convert_scale_fallback (const GESVideoElementSelector * self,
    const gchar * name, GstElement ** main_element)
{
  GstElement *bin, *uploader, *cc_in, *scale, *cc_out, *downloader;
  GstElement *tail;
  GstPad *sink_pad, *src_pad;

  bin = gst_bin_new (name);
  cc_in = gst_element_factory_create (self->colorconvert, NULL);
  scale = gst_element_factory_create (self->scale, NULL);
  cc_out = gst_element_factory_create (self->colorconvert, NULL);
  if (main_element)
    *main_element = scale;
  gst_bin_add_many (GST_BIN (bin), cc_in, scale, cc_out, NULL);
  gst_element_link_many (cc_in, scale, cc_out, NULL);
  tail = cc_out;

  if (self->uploader) {
    uploader = gst_element_factory_create (self->uploader, NULL);
    gst_bin_add (GST_BIN (bin), uploader);
    gst_element_link (uploader, cc_in);
    sink_pad = gst_element_get_static_pad (uploader, "sink");
  } else {
    sink_pad = gst_element_get_static_pad (cc_in, "sink");
  }
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", sink_pad));
  gst_object_unref (sink_pad);

  if (self->downloader && !self->strict) {
    downloader = gst_element_factory_create (self->downloader, NULL);
    gst_bin_add (GST_BIN (bin), downloader);
    gst_element_link (tail, downloader);
    tail = downloader;
  }
  src_pad = gst_element_get_static_pad (tail, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", src_pad));
  gst_object_unref (src_pad);

  return bin;
}

GstElement *
ges_video_element_selector_make_convert_scale (const GESVideoElementSelector *
    self, const gchar * name, GstElement ** main_element)
{
  g_return_val_if_fail (self != NULL, NULL);

  if (self->convert_scale)
    return make_wrapped_element (self, self->convert_scale, name, main_element);

  g_return_val_if_fail (self->scale != NULL, NULL);
  if (self->sw_fallback_mask & GES_SELECTOR_FALLBACK_SCALE)
    return make_sysmem_fallback_wrapped (self, self->scale, name, main_element);
  return make_convert_scale_fallback (self, name, main_element);
}

/* Sysmem `downloader ! videoscale ! uploader` bin. Used as a
 * fallback when the native scaler doesn't carry a property the
 * caller needs (e.g. `add-borders`). Wasteful but correct. */
static GstElement *
make_sysmem_scale_wrapper (const GESVideoElementSelector * self,
    const gchar * name, GstElement ** main_element)
{
  GstElement *bin, *downloader, *sw_scale, *uploader;
  GstPad *sink, *src;

  bin = gst_bin_new (name);
  downloader = gst_element_factory_create (self->downloader, NULL);
  sw_scale = gst_element_factory_make ("videoscale", NULL);
  uploader = gst_element_factory_create (self->uploader, NULL);

  gst_bin_add_many (GST_BIN (bin), downloader, sw_scale, uploader, NULL);
  gst_element_link_many (downloader, sw_scale, uploader, NULL);

  sink = gst_element_get_static_pad (downloader, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", sink));
  gst_object_unref (sink);
  src = gst_element_get_static_pad (uploader, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", src));
  gst_object_unref (src);

  if (main_element)
    *main_element = sw_scale;
  return bin;
}

/* Like _make_convert_scale, but guarantees `main_element` exposes
 * `add-borders`. Falls back to a sysmem-wrapped videoscale when the
 * backend's scaler doesn't carry the property (today: `glcolorscale`). */
GstElement *
ges_video_element_selector_make_convert_scale_add_borders (const
    GESVideoElementSelector * self, const gchar * name,
    GstElement ** main_element)
{
  GstElement *scale, *core = NULL;

  g_return_val_if_fail (self != NULL, NULL);

  scale = ges_video_element_selector_make_convert_scale (self, name, &core);
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (core), "add-borders")) {
    if (main_element)
      *main_element = core;
    return scale;
  }

  GST_WARNING_ONCE ("Backend '%s' has no scaler exposing `add-borders`; "
      "falling back to sysmem videoscale wrapped with %s/%s.",
      self->memory_feature ? self->memory_feature : "software",
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->downloader)),
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->uploader)));
  gst_object_unref (scale);
  return make_sysmem_scale_wrapper (self, name, main_element);
}

GstElement *
ges_video_element_selector_make_deinterlace (const GESVideoElementSelector *
    self, const gchar * name, GstElement ** main_element)
{
  return make_role (self, self->deinterlace,
      GES_SELECTOR_FALLBACK_DEINTERLACE, name, main_element);
}

GstElement *
ges_video_element_selector_make_videoflip (const GESVideoElementSelector * self,
    const gchar * name, GstElement ** main_element)
{
  return make_role (self, self->videoflip,
      GES_SELECTOR_FALLBACK_VIDEOFLIP, name, main_element);
}

GstElement *
ges_video_element_selector_make_uploader (const GESVideoElementSelector * self)
{
  g_return_val_if_fail (self != NULL, NULL);
  if (!self->strict || !self->uploader)
    return NULL;
  return gst_element_factory_create (self->uploader, NULL);
}

gchar *
ges_video_element_selector_colorconvert_bin_desc (const GESVideoElementSelector
    * self)
{
  GString *str;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->colorconvert != NULL, NULL);

  str = g_string_new (NULL);
  if (self->uploader)
    g_string_append_printf (str, "%s ! ",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->uploader)));
  g_string_append (str,
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->colorconvert)));
  if (self->downloader && !self->strict)
    g_string_append_printf (str, " ! %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->downloader)));

  return g_string_free (str, FALSE);
}

GstStructure *
ges_video_element_selector_describe (const GESVideoElementSelector * self)
{
  GstStructure *s;

  g_return_val_if_fail (self != NULL, NULL);

  s = gst_structure_new ("ges-video-element-selector",
      "strict", G_TYPE_BOOLEAN, self->strict, NULL);
  if (self->memory_feature)
    gst_structure_set (s, "memory-feature", G_TYPE_STRING,
        self->memory_feature, NULL);

#define SET_FACTORY(field, name) G_STMT_START { \
    if (self->field) \
      gst_structure_set (s, name, G_TYPE_STRING, \
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (self->field)), \
          NULL); \
  } G_STMT_END
  SET_FACTORY (compositor, "compositor-factory");
  SET_FACTORY (uploader, "uploader-factory");
  SET_FACTORY (downloader, "downloader-factory");
  SET_FACTORY (colorconvert, "colorconvert-factory");
  SET_FACTORY (convert_scale, "convert-scale-factory");
  SET_FACTORY (scale, "scale-factory");
  SET_FACTORY (videoflip, "videoflip-factory");
  SET_FACTORY (deinterlace, "deinterlace-factory");
#undef SET_FACTORY

  return s;
}
