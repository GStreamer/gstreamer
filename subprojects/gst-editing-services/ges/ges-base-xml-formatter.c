/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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

#include "ges.h"
#include "ges-internal.h"

GST_DEBUG_CATEGORY_STATIC (base_xml_formatter);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT base_xml_formatter

#define parent_class ges_base_xml_formatter_parent_class

#define _GET_PRIV(o) (((GESBaseXmlFormatter*) o)->priv)


static gboolean _loading_done_cb (GESFormatter * self);

typedef struct PendingGroup
{
  GESGroup *group;

  GList *pending_children;
} PendingGroup;

typedef struct LayerEntry
{
  GESLayer *layer;
  gboolean auto_trans;
} LayerEntry;

typedef struct PendingAsset
{
  GESFormatter *formatter;
  gchar *metadatas;
  GstStructure *properties;
  gchar *proxy_id;
  GType extractable_type;
  gchar *id;
} PendingAsset;

/* @STATE_CHECK_LOADABLE: Quickly check if XML is valid
 * @STATE_ASSETS: start loading all assets asynchronously
 * and setup all elements that are synchronously loadable (tracks, and layers basically).
 * @STATE_LOADING_CLIPS: adding clips and groups to the timeline
 */
typedef enum
{
  STATE_CHECK_LOADABLE,
  STATE_LOADING_ASSETS_AND_SYNC,
  STATE_LOADING_CLIPS,
} LoadingState;

struct _GESBaseXmlFormatterPrivate
{
  GMarkupParseContext *parsecontext;
  gsize xmlsize;
  LoadingState state;

  /* Clip.ID -> Clip */
  GHashTable *containers;

  /* ID -> track */
  GHashTable *tracks;

  /* layer.prio -> LayerEntry */
  GHashTable *layers;

  /* List of asset waited to be created */
  GList *pending_assets;

  GError *asset_error;

  /* current track element */
  GESTrackElement *current_track_element;

  GESClip *current_clip;
  GstClockTime current_clip_duration;

  gboolean timeline_auto_transition;

  GList *groups;
};

static void new_asset_cb (GESAsset * source, GAsyncResult * res,
    PendingAsset * passet);

static const gchar *
loading_state_name (LoadingState state)
{
  switch (state) {
    case STATE_CHECK_LOADABLE:
      return "check-loadable";
    case STATE_LOADING_ASSETS_AND_SYNC:
      return "loading-assets-and-sync";
    case STATE_LOADING_CLIPS:
      return "loading-clips";
  }

  return "??";
}


static void
_free_layer_entry (LayerEntry * entry)
{
  gst_object_unref (entry->layer);
  g_free (entry);
}

static void
_free_pending_group (PendingGroup * pgroup)
{
  if (pgroup->group)
    g_object_unref (pgroup->group);
  g_list_free_full (pgroup->pending_children, g_free);
  g_free (pgroup);
}

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GESBaseXmlFormatter,
    ges_base_xml_formatter, GES_TYPE_FORMATTER);
static gint
compare_assets_for_loading (PendingAsset * a, PendingAsset * b)
{
  if (a->extractable_type == GES_TYPE_TIMELINE)
    return -1;

  if (b->extractable_type == GES_TYPE_TIMELINE)
    return 1;

  if (a->proxy_id)
    return -1;

  if (b->proxy_id)
    return 1;

  return 0;
}

static GMarkupParseContext *
_parse (GESBaseXmlFormatter * self, GError ** error, LoadingState state)
{
  GError *err = NULL;
  GMarkupParseContext *parsecontext = NULL;
  GESBaseXmlFormatterClass *self_class =
      GES_BASE_XML_FORMATTER_GET_CLASS (self);
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (!self->xmlcontent || g_strcmp0 (self->xmlcontent, "") == 0) {
    err = g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Nothing contained in the project file.");

    goto failed;
  }

  parsecontext = g_markup_parse_context_new (&self_class->content_parser,
      G_MARKUP_TREAT_CDATA_AS_TEXT, self, NULL);

  priv->state = state;
  GST_DEBUG_OBJECT (self, "Running %s pass", loading_state_name (state));
  if (!g_markup_parse_context_parse (parsecontext, self->xmlcontent,
          priv->xmlsize, &err))
    goto failed;

  if (!g_markup_parse_context_end_parse (parsecontext, &err))
    goto failed;

  if (priv->pending_assets) {
    GList *tmp;
    priv->pending_assets = g_list_sort (priv->pending_assets,
        (GCompareFunc) compare_assets_for_loading);

    for (tmp = priv->pending_assets; tmp; tmp = tmp->next) {
      PendingAsset *passet = tmp->data;

      ges_asset_request_async (passet->extractable_type, passet->id, NULL,
          (GAsyncReadyCallback) new_asset_cb, passet);
      ges_project_add_loading_asset (GES_FORMATTER (self)->project,
          passet->extractable_type, passet->id);
    }
  }

done:
  return parsecontext;

failed:
  GST_WARNING ("failed to load contents: %s", err->message);
  g_propagate_error (error, err);

  if (parsecontext) {
    g_markup_parse_context_free (parsecontext);
    parsecontext = NULL;
  }

  goto done;
}

static GMarkupParseContext *
_load_and_parse (GESBaseXmlFormatter * self, const gchar * uri, GError ** error,
    LoadingState state)
{
  GFile *file = NULL;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  GError *err = NULL;

  GST_DEBUG_OBJECT (self, "loading xml from %s, %s", uri,
      loading_state_name (state));

  file = g_file_new_for_uri (uri);
  /* TODO Handle GCancellable */
  if (!g_file_query_exists (file, NULL)) {
    err = g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Invalid URI: \"%s\"", uri);
    goto failed;
  }

  g_clear_pointer (&self->xmlcontent, g_free);
  if (!g_file_load_contents (file, NULL, &self->xmlcontent, &priv->xmlsize,
          NULL, &err))
    goto failed;
  g_object_unref (file);

  return _parse (self, error, state);

failed:
  g_object_unref (file);
  GST_INFO_OBJECT (self, "failed to load contents from \"%s\"", uri);
  g_propagate_error (error, err);
  return NULL;
}

/***********************************************
 *                                             *
 * GESFormatter virtual methods implementation *
 *                                             *
 ***********************************************/

static gboolean
_can_load_uri (GESFormatter * dummy_formatter, const gchar * uri,
    GError ** error)
{
  GMarkupParseContext *ctx;
  GESBaseXmlFormatter *self = GES_BASE_XML_FORMATTER (dummy_formatter);

  ctx = _load_and_parse (self, uri, error, STATE_CHECK_LOADABLE);
  if (!ctx)
    return FALSE;

  g_markup_parse_context_free (ctx);
  return TRUE;
}

static gboolean
_load_from_uri (GESFormatter * self, GESTimeline * timeline, const gchar * uri,
    GError ** error)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  GST_INFO_OBJECT (self, "Loading %s in %" GST_PTR_FORMAT, uri, timeline);
  ges_timeline_set_auto_transition (timeline, FALSE);

  priv->parsecontext =
      _load_and_parse (GES_BASE_XML_FORMATTER (self), uri, error,
      STATE_LOADING_ASSETS_AND_SYNC);

  if (!priv->parsecontext)
    return FALSE;

  if (priv->pending_assets == NULL)
    ges_idle_add ((GSourceFunc) _loading_done_cb, g_object_ref (self), NULL);

  return TRUE;
}

static gboolean
_save_to_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri, gboolean overwrite, GError ** error)
{
  GFile *file;
  gboolean ret;
  GString *str;
  GOutputStream *stream;
  GError *lerror = NULL;

  g_return_val_if_fail (formatter->project, FALSE);

  file = g_file_new_for_uri (uri);
  stream = G_OUTPUT_STREAM (g_file_create (file, G_FILE_CREATE_NONE, NULL,
          &lerror));
  if (stream == NULL) {
    if (overwrite && lerror->code == G_IO_ERROR_EXISTS) {
      g_clear_error (&lerror);
      stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE,
              G_FILE_CREATE_NONE, NULL, &lerror));
    }

    if (stream == NULL)
      goto failed_opening_file;
  }

  str = GES_BASE_XML_FORMATTER_GET_CLASS (formatter)->save (formatter,
      timeline, error);

  if (str == NULL)
    goto serialization_failed;

  ret = g_output_stream_write_all (stream, str->str, str->len, NULL,
      NULL, &lerror);
  ret = g_output_stream_close (stream, NULL, &lerror);

  if (ret == FALSE)
    GST_WARNING_OBJECT (formatter, "Could not save %s because: %s", uri,
        lerror->message);

  g_string_free (str, TRUE);
  gst_object_unref (file);
  gst_object_unref (stream);

  if (lerror)
    g_propagate_error (error, lerror);

  return ret;

serialization_failed:
  gst_object_unref (file);

  g_output_stream_close (stream, NULL, NULL);
  gst_object_unref (stream);
  if (lerror)
    g_propagate_error (error, lerror);

  return FALSE;

failed_opening_file:
  gst_object_unref (file);

  GST_WARNING_OBJECT (formatter, "Could not open %s because: %s", uri,
      lerror->message);

  if (lerror)
    g_propagate_error (error, lerror);

  return FALSE;
}

/***********************************************
 *                                             *
 *   GOBject virtual methods implementation    *
 *                                             *
 ***********************************************/

static void
_dispose (GObject * object)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (object);

  g_clear_pointer (&priv->containers, g_hash_table_unref);
  g_clear_pointer (&priv->tracks, g_hash_table_unref);
  g_clear_pointer (&priv->layers, g_hash_table_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
_finalize (GObject * object)
{
  GESBaseXmlFormatter *self = GES_BASE_XML_FORMATTER (object);
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (object);

  if (priv->parsecontext != NULL)
    g_markup_parse_context_free (priv->parsecontext);
  g_clear_pointer (&self->xmlcontent, g_free);

  g_list_free_full (priv->groups, (GDestroyNotify) _free_pending_group);
  priv->groups = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
ges_base_xml_formatter_init (GESBaseXmlFormatter * self)
{
  GESBaseXmlFormatterPrivate *priv;

  self->priv = ges_base_xml_formatter_get_instance_private (self);

  priv = self->priv;

  priv->parsecontext = NULL;
  priv->pending_assets = NULL;

  priv->containers = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, gst_object_unref);
  priv->tracks = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, gst_object_unref);
  priv->layers = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) _free_layer_entry);
  priv->current_track_element = NULL;
  priv->current_clip = NULL;
  priv->current_clip_duration = GST_CLOCK_TIME_NONE;
  priv->timeline_auto_transition = FALSE;
}

static void
ges_base_xml_formatter_class_init (GESBaseXmlFormatterClass * self_class)
{
  GESFormatterClass *formatter_klass = GES_FORMATTER_CLASS (self_class);
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = _dispose;
  object_class->finalize = _finalize;

  formatter_klass->can_load_uri = _can_load_uri;
  formatter_klass->load_from_uri = _load_from_uri;
  formatter_klass->save_to_uri = _save_to_uri;

  self_class->save = NULL;

  GST_DEBUG_CATEGORY_INIT (base_xml_formatter, "gesbasexmlformatter",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "Base XML Formatter");
}

/***********************************************
 *                                             *
 *             Private methods                 *
 *                                             *
 ***********************************************/


static GESTrackElement *
_get_element_by_track_id (GESBaseXmlFormatterPrivate * priv,
    const gchar * track_id, GESClip * clip)
{
  GESTrack *track = g_hash_table_lookup (priv->tracks, track_id);

  return ges_clip_find_track_element (clip, track, GES_TYPE_SOURCE);
}

static void
_set_auto_transition (gpointer prio, LayerEntry * entry, gpointer udata)
{
  ges_layer_set_auto_transition (entry->layer, entry->auto_trans);
}

static void
_add_all_groups (GESFormatter * self)
{
  GList *tmp;
  GESTimelineElement *child;
  GESBaseXmlFormatterPrivate *priv = GES_BASE_XML_FORMATTER (self)->priv;

  for (tmp = priv->groups; tmp; tmp = tmp->next) {
    GList *lchild;
    PendingGroup *pgroup = tmp->data;

    timeline_add_group (self->timeline, pgroup->group);

    for (lchild = ((PendingGroup *) tmp->data)->pending_children; lchild;
        lchild = lchild->next) {
      child = g_hash_table_lookup (priv->containers, lchild->data);

      GST_DEBUG_OBJECT (pgroup->group, "Adding %s child %" GST_PTR_FORMAT " %s",
          (const gchar *) lchild->data, child,
          GES_TIMELINE_ELEMENT_NAME (child));
      if (!ges_container_add (GES_CONTAINER (pgroup->group), child)) {
        GST_ERROR_OBJECT (pgroup->group,
            "%" GES_FORMAT " could not add child %p while"
            " reloading, this should never happen", GES_ARGS (pgroup->group),
            child);
      }
    }
    pgroup->group = NULL;
  }

  g_list_free_full (priv->groups, (GDestroyNotify) _free_pending_group);
  priv->groups = NULL;
}

static void
_loading_done (GESFormatter * self)
{
  GList *assets, *tmp;
  GError *error = NULL;
  GESBaseXmlFormatterPrivate *priv = GES_BASE_XML_FORMATTER (self)->priv;

  if (priv->parsecontext)
    g_markup_parse_context_free (priv->parsecontext);
  priv->parsecontext = NULL;
  /* Go over all assets and make sure that all proxies we were 'trying' to set are finally
   * properly set */
  assets = ges_project_list_assets (self->project, GES_TYPE_EXTRACTABLE);
  for (tmp = assets; tmp; tmp = tmp->next) {
    ges_asset_finish_proxy (tmp->data);
  }
  g_list_free_full (assets, g_object_unref);

  if (priv->asset_error) {
    error = priv->asset_error;
    priv->asset_error = NULL;
  } else if (priv->state == STATE_LOADING_ASSETS_AND_SYNC) {
    GMarkupParseContext *context =
        _parse (GES_BASE_XML_FORMATTER (self), &error, STATE_LOADING_CLIPS);
    GST_INFO_OBJECT (self, "Assets cached... now loading the timeline.");

    if (context)
      g_markup_parse_context_free (context);
    g_assert (priv->pending_assets == NULL);
  }

  _add_all_groups (self);
  ges_timeline_set_auto_transition (self->timeline,
      priv->timeline_auto_transition);

  g_hash_table_foreach (priv->layers, (GHFunc) _set_auto_transition, NULL);
  ges_project_set_loaded (self->project, self, error);
  g_clear_error (&error);
}

static gboolean
_loading_done_cb (GESFormatter * self)
{
  _loading_done (self);
  gst_object_unref (self);

  return FALSE;
}

static gboolean
_set_child_property (GQuark field_id, const GValue * value,
    GESTimelineElement * tlelement)
{
  GParamSpec *pspec;
  GObject *object;

  /* FIXME: error handling? */
  if (!ges_timeline_element_lookup_child (tlelement,
          g_quark_to_string (field_id), &object, &pspec)) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar *tmp = gst_value_serialize (value);
    GST_ERROR_OBJECT (tlelement, "Could not set %s=%s",
        g_quark_to_string (field_id), tmp);
    g_free (tmp);
#endif
    return TRUE;
  }

  g_object_set_property (G_OBJECT (object), pspec->name, value);
  g_param_spec_unref (pspec);
  gst_object_unref (object);
  return TRUE;
}

gboolean
set_property_foreach (GQuark field_id, const GValue * value, GObject * object)
{
  g_object_set_property (object, g_quark_to_string (field_id), value);
  return TRUE;
}

static inline GESClip *
_add_object_to_layer (GESBaseXmlFormatterPrivate * priv, const gchar * id,
    GESLayer * layer, GESAsset * asset, GstClockTime start,
    GstClockTime inpoint, GstClockTime duration,
    GESTrackType track_types, const gchar * metadatas,
    GstStructure * properties, GstStructure * children_properties,
    GError ** error)
{
  GESClip *clip = ges_layer_add_asset (layer,
      asset, start, inpoint, duration, track_types);

  if (clip == NULL) {
    g_set_error (error, GES_ERROR, GES_ERROR_FORMATTER_MALFORMED_INPUT_FILE,
        "Could not add clip %s [ %" GST_TIME_FORMAT ", ( %" GST_TIME_FORMAT
        ") - %" GST_TIME_FORMAT "]", id, GST_TIME_ARGS (start),
        GST_TIME_ARGS (inpoint), GST_TIME_ARGS (duration));

    return NULL;
  }

  if (metadatas)
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (clip),
        metadatas);

  if (properties)
    gst_structure_foreach (properties,
        (GstStructureForeachFunc) set_property_foreach, clip);

  if (children_properties)
    gst_structure_foreach (children_properties,
        (GstStructureForeachFunc) _set_child_property, clip);

  g_hash_table_insert (priv->containers, g_strdup (id), gst_object_ref (clip));
  return clip;
}

static void
_add_track_element (GESFormatter * self, GESClip * clip,
    GESTrackElement * trackelement, const gchar * track_id,
    GstStructure * children_properties, GstStructure * properties)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);
  GESTrack *track = g_hash_table_lookup (priv->tracks, track_id);

  if (track == NULL) {
    GST_WARNING_OBJECT (self, "No track with id %s, can not add trackelement",
        track_id);
    gst_object_unref (trackelement);
    return;
  }

  GST_DEBUG_OBJECT (self, "Adding track_element: %" GST_PTR_FORMAT
      " To : %" GST_PTR_FORMAT, trackelement, clip);

  if (!ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (trackelement)))
    GST_ERROR ("%" GES_FORMAT " could not add child %p while"
        " reloading, this should never happen", GES_ARGS (clip), trackelement);
  gst_structure_foreach (children_properties,
      (GstStructureForeachFunc) _set_child_property, trackelement);

  if (properties) {
    gboolean has_internal_source;
    /* We do not serialize the priority anymore, and we should never have. */
    gst_structure_remove_field (properties, "priority");

    /* Ensure that has-internal-source is set before inpoint as otherwise
     * the inpoint will be ignored */
    if (gst_structure_get_boolean (properties, "has-internal-source",
            &has_internal_source) && has_internal_source)
      g_object_set (trackelement, "has-internal-source", has_internal_source,
          NULL);
    gst_structure_foreach (properties,
        (GstStructureForeachFunc) set_property_foreach, trackelement);
  }
}

static void
_free_pending_asset (GESBaseXmlFormatterPrivate * priv, PendingAsset * passet)
{
  g_free (passet->metadatas);
  g_free (passet->id);
  g_free (passet->proxy_id);
  if (passet->properties)
    gst_structure_free (passet->properties);

  priv->pending_assets = g_list_remove (priv->pending_assets, passet);
  g_free (passet);
}

static void
new_asset_cb (GESAsset * source, GAsyncResult * res, PendingAsset * passet)
{
  GError *error = NULL;
  gchar *possible_id = NULL;
  GESFormatter *self = passet->formatter;
  const gchar *id = ges_asset_get_id (source);
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);
  GESAsset *asset = ges_asset_request_finish (res, &error);

  if (error) {
    GST_INFO_OBJECT (self, "Error %s creating asset id: %s", error->message,
        id);

    /* We set the metas on the Asset to give hints to the user */
    if (passet->metadatas)
      ges_meta_container_add_metas_from_string (GES_META_CONTAINER (source),
          passet->metadatas);
    if (passet->properties)
      gst_structure_foreach (passet->properties,
          (GstStructureForeachFunc) set_property_foreach, source);

    possible_id = ges_project_try_updating_id (GES_FORMATTER (self)->project,
        source, error);

    if (possible_id == NULL) {
      GST_WARNING_OBJECT (self, "Abandoning creation of asset %s with ID %s"
          "- Error: %s", g_type_name (G_OBJECT_TYPE (source)), id,
          error->message);

      _free_pending_asset (priv, passet);
      if (!priv->asset_error)
        priv->asset_error = g_error_copy (error);
      goto done;
    }

    /* We got a possible ID replacement for that asset, create it */
    ges_asset_request_async (ges_asset_get_extractable_type (source),
        possible_id, NULL, (GAsyncReadyCallback) new_asset_cb, passet);
    ges_project_add_loading_asset (GES_FORMATTER (self)->project,
        ges_asset_get_extractable_type (source), possible_id);

    goto done;
  }

  if (passet->proxy_id) {
    /* We set the URI to be used as a proxy,
     * this will finally be set as the proxy when we
     * are done loading all assets */
    ges_asset_try_proxy (asset, passet->proxy_id);
  }

  if (passet->metadatas)
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (asset),
        passet->metadatas);

  /* And now add to the project */
  ges_project_add_asset (self->project, asset);
  gst_object_unref (self);

  _free_pending_asset (priv, passet);

done:
  if (asset)
    gst_object_unref (asset);
  if (possible_id)
    g_free (possible_id);

  g_clear_error (&error);

  if (priv->pending_assets == NULL)
    _loading_done (self);
}

GstElement *
get_element_for_encoding_profile (GstEncodingProfile * prof,
    GstElementFactoryListType type)
{
  GstEncodingProfile *prof_copy;
  GstElement *encodebin;
  GList *tmp;
  GstElement *element = NULL;

  prof_copy = gst_encoding_profile_copy (prof);

  gst_encoding_profile_set_presence (prof_copy, 1);
  gst_encoding_profile_set_preset (prof_copy, NULL);

  encodebin = gst_element_factory_make ("encodebin", NULL);
  g_object_set (encodebin, "profile", prof_copy, NULL);

  GST_OBJECT_LOCK (encodebin);
  for (tmp = GST_BIN (encodebin)->children; tmp; tmp = tmp->next) {
    GstElementFactory *factory;
    factory = gst_element_get_factory (GST_ELEMENT (tmp->data));

    if (factory && gst_element_factory_list_is_type (factory, type)) {
      element = GST_ELEMENT (tmp->data);
      gst_object_ref (element);
      break;
    }
  }
  GST_OBJECT_UNLOCK (encodebin);
  gst_object_unref (encodebin);

  gst_encoding_profile_unref (prof_copy);

  return element;
}

static GstEncodingProfile *
_create_profile (GESBaseXmlFormatter * self,
    const gchar * type, const gchar * parent, const gchar * name,
    const gchar * description, GstCaps * format, const gchar * preset,
    GstStructure * preset_properties, const gchar * preset_name, gint id,
    guint presence, GstCaps * restriction, guint pass,
    gboolean variableframerate, gboolean enabled)
{
  GstEncodingProfile *profile = NULL;

  if (!g_strcmp0 (type, "container")) {
    profile = GST_ENCODING_PROFILE (gst_encoding_container_profile_new (name,
            description, format, preset));
    gst_encoding_profile_set_preset_name (profile, preset_name);
  } else if (!g_strcmp0 (type, "video")) {
    GstEncodingVideoProfile *sprof = gst_encoding_video_profile_new (format,
        preset, restriction, presence);

    gst_encoding_video_profile_set_variableframerate (sprof, variableframerate);
    gst_encoding_video_profile_set_pass (sprof, pass);

    profile = GST_ENCODING_PROFILE (sprof);
  } else if (!g_strcmp0 (type, "audio")) {
    profile = GST_ENCODING_PROFILE (gst_encoding_audio_profile_new (format,
            preset, restriction, presence));
  } else {
    GST_ERROR_OBJECT (self, "Unknown profile format '%s'", type);

    return NULL;
  }

  if (!g_strcmp0 (type, "video") || !g_strcmp0 (type, "audio")) {
    gst_encoding_profile_set_name (profile, name);
    gst_encoding_profile_set_enabled (profile, enabled);
    gst_encoding_profile_set_description (profile, description);
    gst_encoding_profile_set_preset_name (profile, preset_name);
  }
  if (preset_properties) {
    gst_encoding_profile_set_element_properties (profile,
        gst_structure_copy (preset_properties));
  }

  return profile;
}

/***********************************************
 *                                             *
 *              Public methods                 *
 *                                             *
 ***********************************************/

void
ges_base_xml_formatter_add_asset (GESBaseXmlFormatter * self,
    const gchar * id, GType extractable_type, GstStructure * properties,
    const gchar * metadatas, const gchar * proxy_id, GError ** error)
{
  PendingAsset *passet;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_ASSETS_AND_SYNC) {
    GST_DEBUG_OBJECT (self, "Not parsing assets in %s state",
        loading_state_name (priv->state));

    return;
  }

  passet = g_new0 (PendingAsset, 1);
  passet->metadatas = g_strdup (metadatas);
  passet->id = g_strdup (id);
  passet->extractable_type = extractable_type;
  passet->proxy_id = g_strdup (proxy_id);
  passet->formatter = gst_object_ref (self);
  if (properties)
    passet->properties = gst_structure_copy (properties);
  priv->pending_assets = g_list_prepend (priv->pending_assets, passet);
}

void
ges_base_xml_formatter_add_clip (GESBaseXmlFormatter * self,
    const gchar * id, const char *asset_id, GType type, GstClockTime start,
    GstClockTime inpoint, GstClockTime duration,
    guint layer_prio, GESTrackType track_types, GstStructure * properties,
    GstStructure * children_properties,
    const gchar * metadatas, GError ** error)
{
  GESAsset *asset;
  GESClip *nclip;
  LayerEntry *entry;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_CLIPS) {
    GST_DEBUG_OBJECT (self, "Not adding clip in %s state.",
        loading_state_name (priv->state));
    return;
  }

  entry = g_hash_table_lookup (priv->layers, GINT_TO_POINTER (layer_prio));
  if (entry == NULL) {
    g_set_error (error, GES_ERROR, GES_ERROR_FORMATTER_MALFORMED_INPUT_FILE,
        "We got a Clip in a layer"
        " that does not exist, something is wrong either in the project file or"
        " in %s", g_type_name (G_OBJECT_TYPE (self)));
    return;
  }

  /* We do not want the properties that are passed to layer-add_asset to be reset */
  if (properties)
    gst_structure_remove_fields (properties, "supported-formats",
        "inpoint", "start", "duration", NULL);

  asset = ges_asset_request (type, asset_id, NULL);
  if (!asset) {
    g_set_error (error, GES_ERROR, GES_ERROR_FORMATTER_MALFORMED_INPUT_FILE,
        "Clip references asset %s of type %s which was not present in the list of ressource,"
        " the file seems to be malformed.", asset_id, g_type_name (type));
    return;
  }

  nclip = _add_object_to_layer (priv, id, entry->layer,
      asset, start, inpoint, duration, track_types, metadatas, properties,
      children_properties, error);

  gst_object_unref (asset);
  if (!nclip)
    return;

  priv->current_clip_duration = duration;
  priv->current_clip = nclip;
}

void
ges_base_xml_formatter_set_timeline_properties (GESBaseXmlFormatter * self,
    GESTimeline * timeline, const gchar * properties, const gchar * metadatas)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);
  gboolean auto_transition = FALSE;

  if (properties) {
    GstStructure *props = gst_structure_from_string (properties, NULL);

    if (props) {
      if (gst_structure_get_boolean (props, "auto-transition",
              &auto_transition))
        gst_structure_remove_field (props, "auto-transition");

      gst_structure_foreach (props,
          (GstStructureForeachFunc) set_property_foreach, timeline);
      gst_structure_free (props);
    }
  }

  if (metadatas) {
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (timeline),
        metadatas);
  };

  priv->timeline_auto_transition = auto_transition;
}

void
ges_base_xml_formatter_add_layer (GESBaseXmlFormatter * self,
    GType extractable_type, guint priority, GstStructure * properties,
    const gchar * metadatas, gchar ** deactivated_tracks, GError ** error)
{
  LayerEntry *entry;
  GESAsset *asset;
  GESLayer *layer;
  gboolean auto_transition = FALSE;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_ASSETS_AND_SYNC) {
    GST_INFO_OBJECT (self, "Not loading layer in %s state.",
        loading_state_name (priv->state));
    return;
  }

  if (extractable_type == G_TYPE_NONE)
    layer = ges_layer_new ();
  else {
    asset = ges_asset_request (extractable_type, NULL, error);
    if (asset == NULL) {
      if (error && *error == NULL) {
        g_set_error (error, G_MARKUP_ERROR,
            G_MARKUP_ERROR_INVALID_CONTENT,
            "Layer type %s could not be created'",
            g_type_name (extractable_type));
      }
      return;
    }
    layer = GES_LAYER (ges_asset_extract (asset, error));
    gst_object_unref (asset);
  }

  ges_layer_set_priority (layer, priority);
  ges_timeline_add_layer (GES_FORMATTER (self)->timeline, layer);
  if (properties) {
    if (gst_structure_get_boolean (properties, "auto-transition",
            &auto_transition))
      gst_structure_remove_field (properties, "auto-transition");

    gst_structure_foreach (properties,
        (GstStructureForeachFunc) set_property_foreach, layer);
  }

  if (metadatas)
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (layer),
        metadatas);

  if (deactivated_tracks) {
    gint i;
    GList *tracks = NULL;

    for (i = 0; deactivated_tracks[i] && deactivated_tracks[i][0] != '\0'; i++) {
      GESTrack *track =
          g_hash_table_lookup (priv->tracks, deactivated_tracks[i]);

      if (!track) {
        GST_ERROR_OBJECT (self,
            "Unknown deactivated track: %s", deactivated_tracks[i]);
        continue;
      }

      tracks = g_list_append (tracks, track);
    }

    ges_layer_set_active_for_tracks (layer, FALSE, tracks);
    g_list_free (tracks);
  }

  entry = g_new0 (LayerEntry, 1);
  entry->layer = gst_object_ref (layer);
  entry->auto_trans = auto_transition;

  g_hash_table_insert (priv->layers, GINT_TO_POINTER (priority), entry);
}

void
ges_base_xml_formatter_add_track (GESBaseXmlFormatter * self,
    GESTrackType track_type, GstCaps * caps, const gchar * id,
    GstStructure * properties, const gchar * metadatas, GError ** error)
{
  GESTrack *track;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_ASSETS_AND_SYNC) {
    GST_INFO_OBJECT (self, "Not loading track in %s state.",
        loading_state_name (priv->state));
    return;
  }

  track = ges_track_new (track_type, caps);
  ges_timeline_add_track (GES_FORMATTER (self)->timeline, track);

  if (properties) {
    gchar *restriction = NULL;
    GstCaps *restriction_caps;

    if (gst_structure_get (properties, "restriction-caps", G_TYPE_STRING,
            &restriction, NULL) && g_strcmp0 (restriction, "NULL")) {
      restriction_caps = gst_caps_from_string (restriction);
      if (restriction_caps) {
        ges_track_set_restriction_caps (track, restriction_caps);
        gst_caps_unref (restriction_caps);
      } else {
        GST_ERROR_OBJECT (self, "No caps read from the given track property: "
            "restriction-caps=\"%s\"", restriction);
      }
    }
    gst_structure_remove_fields (properties, "restriction-caps", "caps",
        "message-forward", NULL);
    gst_structure_foreach (properties,
        (GstStructureForeachFunc) set_property_foreach, track);
    g_free (restriction);
  }

  g_hash_table_insert (priv->tracks, g_strdup (id), gst_object_ref (track));
  if (metadatas)
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (track),
        metadatas);
}

void
ges_base_xml_formatter_add_control_binding (GESBaseXmlFormatter * self,
    const gchar * binding_type, const gchar * source_type,
    const gchar * property_name, gint mode, const gchar * track_id,
    GSList * timed_values)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);
  GESTrackElement *element = NULL;

  if (priv->state != STATE_LOADING_CLIPS) {
    GST_DEBUG_OBJECT (self, "Not loading control bindings in %s state.",
        loading_state_name (priv->state));
    goto done;
  }

  if (track_id[0] != '-' && priv->current_clip)
    element = _get_element_by_track_id (priv, track_id, priv->current_clip);
  else
    element = priv->current_track_element;

  if (element == NULL) {
    GST_WARNING ("No current track element to which we can append a binding");
    goto done;
  }

  if (!g_strcmp0 (source_type, "interpolation")) {
    GstControlSource *source;

    source = gst_interpolation_control_source_new ();

    /* add first before setting values to avoid clamping */
    ges_track_element_set_control_source (element, source,
        property_name, binding_type);

    g_object_set (source, "mode", mode, NULL);
    if (!gst_timed_value_control_source_set_from_list
        (GST_TIMED_VALUE_CONTROL_SOURCE (source), timed_values)) {
      GST_ERROR_OBJECT (self, "Could not set timed values on %" GES_FORMAT,
          GES_ARGS (source));
    }

    gst_object_unref (source);
  } else
    GST_WARNING ("This interpolation type is not supported\n");

done:
  g_slist_free_full (timed_values, g_free);
}

void
ges_base_xml_formatter_add_source (GESBaseXmlFormatter * self,
    const gchar * track_id, GstStructure * children_properties,
    GstStructure * properties, const gchar * metadatas)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);
  GESTrackElement *element = NULL;

  if (priv->state != STATE_LOADING_CLIPS) {
    GST_DEBUG_OBJECT (self, "Not loading source elements in %s state.",
        loading_state_name (priv->state));
    return;
  }

  if (track_id[0] != '-' && priv->current_clip)
    element = _get_element_by_track_id (priv, track_id, priv->current_clip);
  else
    element = priv->current_track_element;

  if (element == NULL) {
    GST_WARNING
        ("No current track element to which we can append children properties");
    return;
  }

  if (properties)
    gst_structure_foreach (properties,
        (GstStructureForeachFunc) set_property_foreach, element);

  if (children_properties)
    gst_structure_foreach (children_properties,
        (GstStructureForeachFunc) _set_child_property, element);

  if (metadatas)
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER
        (element), metadatas);
}

void
ges_base_xml_formatter_add_track_element (GESBaseXmlFormatter * self,
    GType track_element_type, const gchar * asset_id, const gchar * track_id,
    const gchar * timeline_obj_id, GstStructure * children_properties,
    GstStructure * properties, const gchar * metadatas, GError ** error)
{
  GESTrackElement *trackelement;

  GError *err = NULL;
  GESAsset *asset = NULL;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_CLIPS) {
    GST_DEBUG_OBJECT (self, "Not loading track elements in %s state.",
        loading_state_name (priv->state));
    return;
  }

  if (g_type_is_a (track_element_type, GES_TYPE_TRACK_ELEMENT) == FALSE) {
    GST_DEBUG_OBJECT (self, "%s is not a TrackElement, can not create it",
        g_type_name (track_element_type));
    goto out;
  }

  if (g_type_is_a (track_element_type, GES_TYPE_BASE_EFFECT) == FALSE) {
    GST_FIXME_OBJECT (self, "%s currently not supported",
        g_type_name (track_element_type));
    goto out;
  }

  asset = ges_asset_request (track_element_type, asset_id, &err);
  if (asset == NULL) {
    GST_DEBUG_OBJECT (self, "Can not create trackelement %s", asset_id);
    GST_FIXME_OBJECT (self, "Check if missing plugins etc %s",
        err ? err->message : "");

    goto out;
  }

  trackelement = GES_TRACK_ELEMENT (ges_asset_extract (asset, NULL));
  if (trackelement) {
    GESClip *clip;
    if (metadatas)
      ges_meta_container_add_metas_from_string (GES_META_CONTAINER
          (trackelement), metadatas);

    clip = g_hash_table_lookup (priv->containers, timeline_obj_id);
    _add_track_element (GES_FORMATTER (self), clip, trackelement, track_id,
        children_properties, properties);
    priv->current_track_element = trackelement;
  }

  ges_project_add_asset (GES_FORMATTER (self)->project, asset);

out:
  if (asset)
    gst_object_unref (asset);
  if (err)
    g_error_free (err);

  return;
}

void
ges_base_xml_formatter_add_encoding_profile (GESBaseXmlFormatter * self,
    const gchar * type, const gchar * parent, const gchar * name,
    const gchar * description, GstCaps * format, const gchar * preset,
    GstStructure * preset_properties, const gchar * preset_name, guint id,
    guint presence, GstCaps * restriction, guint pass,
    gboolean variableframerate, GstStructure * properties, gboolean enabled,
    GError ** error)
{
  const GList *tmp;
  GstEncodingProfile *profile;
  GstEncodingContainerProfile *parent_profile = NULL;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_ASSETS_AND_SYNC) {
    GST_DEBUG_OBJECT (self, "Not loading encoding profiles in %s state.",
        loading_state_name (priv->state));
    goto done;
  }

  if (parent == NULL) {
    profile =
        _create_profile (self, type, parent, name, description, format, preset,
        preset_properties, preset_name, id, presence, restriction, pass,
        variableframerate, enabled);
    ges_project_add_encoding_profile (GES_FORMATTER (self)->project, profile);
    gst_object_unref (profile);

    goto done;
  }

  for (tmp = ges_project_list_encoding_profiles (GES_FORMATTER (self)->project);
      tmp; tmp = tmp->next) {
    GstEncodingProfile *tmpprofile = GST_ENCODING_PROFILE (tmp->data);

    if (g_strcmp0 (gst_encoding_profile_get_name (tmpprofile),
            gst_encoding_profile_get_name (tmpprofile)) == 0) {

      if (!GST_IS_ENCODING_CONTAINER_PROFILE (tmpprofile)) {
        g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
            "Profile '%s' parent %s is not a container...'", name, parent);
        goto done;
      }

      parent_profile = GST_ENCODING_CONTAINER_PROFILE (tmpprofile);
      break;
    }
  }

  if (parent_profile == NULL) {
    g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
        "Profile '%s' parent %s does not exist'", name, parent);
    goto done;
  }

  profile =
      _create_profile (self, type, parent, name, description, format, preset,
      preset_properties, preset_name, id, presence, restriction, pass,
      variableframerate, enabled);

  if (profile == NULL)
    goto done;

  gst_encoding_container_profile_add_profile (parent_profile, profile);

done:
  if (format)
    gst_caps_unref (format);
  if (restriction)
    gst_caps_unref (restriction);
}

void
ges_base_xml_formatter_add_group (GESBaseXmlFormatter * self,
    const gchar * id, const gchar * properties, const gchar * metadatas)
{
  PendingGroup *pgroup;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_ASSETS_AND_SYNC) {
    GST_DEBUG_OBJECT (self, "Not loading groups in %s state.",
        loading_state_name (priv->state));
    return;
  }

  pgroup = g_new0 (PendingGroup, 1);
  pgroup->group = ges_group_new ();

  if (metadatas)
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER
        (pgroup->group), metadatas);

  g_hash_table_insert (priv->containers, g_strdup (id),
      gst_object_ref (pgroup->group));
  priv->groups = g_list_prepend (priv->groups, pgroup);

  return;
}

void
ges_base_xml_formatter_last_group_add_child (GESBaseXmlFormatter * self,
    const gchar * child_id, const gchar * name)
{
  PendingGroup *pgroup;
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_CLIPS) {
    GST_DEBUG_OBJECT (self, "Not adding children to groups in %s state.",
        loading_state_name (priv->state));

    return;
  }

  g_return_if_fail (priv->groups);

  pgroup = priv->groups->data;

  pgroup->pending_children =
      g_list_prepend (pgroup->pending_children, g_strdup (child_id));

  GST_DEBUG_OBJECT (self, "Adding %s to %s", child_id,
      GES_TIMELINE_ELEMENT_NAME (((PendingGroup *) priv->groups->data)->group));
}

void
ges_base_xml_formatter_end_current_clip (GESBaseXmlFormatter * self)
{
  GESBaseXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->state != STATE_LOADING_CLIPS) {
    GST_DEBUG_OBJECT (self, "Not ending clip in %s state.",
        loading_state_name (priv->state));
    return;
  }

  g_return_if_fail (priv->current_clip);

  if (_DURATION (priv->current_clip) != priv->current_clip_duration)
    _set_duration0 (GES_TIMELINE_ELEMENT (priv->current_clip),
        priv->current_clip_duration);

  priv->current_clip = NULL;
  priv->current_clip_duration = GST_CLOCK_TIME_NONE;
}
