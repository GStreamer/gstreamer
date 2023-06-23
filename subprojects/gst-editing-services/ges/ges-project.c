/* GStreamer Editing Services
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
/**
 * SECTION: gesproject
 * @title: GESProject
 * @short_description: A GESAsset that is used to manage projects
 *
 * The #GESProject is used to control a set of #GESAsset and is a
 * #GESAsset with `GES_TYPE_TIMELINE` as @extractable_type itself. That
 * means that you can extract #GESTimeline from a project as followed:
 *
 * ```c
 * GESProject *project;
 * GESTimeline *timeline;
 *
 * project = ges_project_new ("file:///path/to/a/valid/project/uri");
 *
 * // Here you can connect to the various signal to get more infos about
 * // what is happening and recover from errors if possible
 * ...
 *
 * timeline = ges_asset_extract (GES_ASSET (project));
 * ```
 *
 * The #GESProject class offers a higher level API to handle #GESAsset-s.
 * It lets you request new asset, and it informs you about new assets through
 * a set of signals. Also it handles problem such as missing files/missing
 * #GstElement and lets you try to recover from those.
 *
 * ## Subprojects
 *
 * In order to add a subproject, the only thing to do is to add the subproject
 * to the main project:
 *
 * ``` c
 * ges_project_add_asset (project, GES_ASSET (subproject));
 * ```
 * then the subproject will be serialized in the project files. To use
 * the subproject in a timeline, you should use a #GESUriClip with the
 * same subproject URI.
 *
 * When loading a project with subproject, subprojects URIs will be temporary
 * writable local files. If you want to edit the subproject timeline,
 * you should retrieve the subproject from the parent project asset list and
 * extract the timeline with ges_asset_extract() and save it at
 * the same temporary location.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges.h"
#include "ges-internal.h"

static GPtrArray *new_paths = NULL;
static GHashTable *tried_uris = NULL;

#define GES_PROJECT_LOCK(project) (g_mutex_lock (&project->priv->lock))
#define GES_PROJECT_UNLOCK(project) (g_mutex_unlock (&project->priv->lock))

/* Fields are protected by GES_PROJECT_LOCK */
struct _GESProjectPrivate
{
  GHashTable *assets;
  /* Set of asset ID being loaded */
  GHashTable *loading_assets;
  GHashTable *loaded_with_error;
  GESAsset *formatter_asset;

  GList *formatters;

  gchar *uri;

  GList *encoding_profiles;

  GMutex lock;
};

typedef struct EmitLoadedInIdle
{
  GESProject *project;
  GESTimeline *timeline;
} EmitLoadedInIdle;

enum
{
  LOADING_SIGNAL,
  LOADED_SIGNAL,
  ERROR_LOADING,
  ERROR_LOADING_ASSET,
  ASSET_ADDED_SIGNAL,
  ASSET_REMOVED_SIGNAL,
  MISSING_URI_SIGNAL,
  ASSET_LOADING_SIGNAL,
  LAST_SIGNAL
};

G_DEFINE_TYPE_WITH_PRIVATE (GESProject, ges_project, GES_TYPE_ASSET);

static guint _signals[LAST_SIGNAL] = { 0 };

static guint nb_projects = 0;

/* Find the type that implemented the GESExtractable interface */
static inline const gchar *
_extractable_type_name (GType type)
{
  while (1) {
    if (g_type_is_a (g_type_parent (type), GES_TYPE_EXTRACTABLE))
      type = g_type_parent (type);
    else
      return g_type_name (type);
  }
}

enum
{
  PROP_0,
  PROP_URI,
  PROP_LAST,
};

static GParamSpec *_properties[LAST_SIGNAL] = { 0 };

static gboolean
_emit_loaded_in_idle (EmitLoadedInIdle * data)
{
  g_signal_emit (data->project, _signals[LOADED_SIGNAL], 0, data->timeline);

  gst_object_unref (data->project);
  gst_object_unref (data->timeline);
  g_free (data);

  return FALSE;
}

/**
 * ges_project_add_formatter:
 * @project: The project to add a formatter to
 * @formatter: A formatter used by @project
 *
 * Adds a formatter to be used to load @project
 *
 * Since: 1.18
 *
 * MT safe.
 */
void
ges_project_add_formatter (GESProject * project, GESFormatter * formatter)
{
  GESProjectPrivate *priv = GES_PROJECT (project)->priv;

  ges_formatter_set_project (formatter, project);
  GES_PROJECT_LOCK (project);
  priv->formatters = g_list_append (priv->formatters, formatter);
  GES_PROJECT_UNLOCK (project);

  gst_object_ref_sink (formatter);
}

/* Internally takes project mutex */
static void
ges_project_remove_formatter (GESProject * project, GESFormatter * formatter)
{
  GList *tmp;
  GESProjectPrivate *priv = GES_PROJECT (project)->priv;

  GES_PROJECT_LOCK (project);
  for (tmp = priv->formatters; tmp; tmp = tmp->next) {
    if (tmp->data == formatter) {
      gst_object_unref (formatter);
      priv->formatters = g_list_delete_link (priv->formatters, tmp);

      goto done;
    }
  }

done:
  GES_PROJECT_UNLOCK (project);
}

/* Internally takes project mutex */
static void
ges_project_set_uri (GESProject * project, const gchar * uri)
{
  GESProjectPrivate *priv;

  g_return_if_fail (GES_IS_PROJECT (project));

  GES_PROJECT_LOCK (project);

  priv = project->priv;
  if (priv->uri) {
    if (g_strcmp0 (priv->uri, uri))
      GST_WARNING_OBJECT (project, "Trying to reset URI, this is prohibited");

    goto done;
  }

  if (uri == NULL) {
    GST_LOG_OBJECT (project, "Uri should not be NULL");
    goto done;
  }

  priv->uri = g_strdup (uri);

  /* We use that URI as ID */
  ges_asset_set_id (GES_ASSET (project), uri);

done:
  GES_PROJECT_UNLOCK (project);
}

/* Internally takes project mutex */
static gboolean
_load_project (GESProject * project, GESTimeline * timeline, GError ** error)
{
  GError *lerr = NULL;
  GESProjectPrivate *priv;
  GESFormatter *formatter;
  gboolean has_uri = FALSE;
  gchar *uri = NULL;

  priv = GES_PROJECT (project)->priv;

  g_signal_emit (project, _signals[LOADING_SIGNAL], 0, timeline);

  GES_PROJECT_LOCK (project);
  has_uri = priv->uri != NULL;
  GES_PROJECT_UNLOCK (project);

  if (!has_uri) {
    const gchar *id = ges_asset_get_id (GES_ASSET (project));

    if (id && gst_uri_is_valid (id)) {
      ges_project_set_uri (project, ges_asset_get_id (GES_ASSET (project)));
      GST_INFO_OBJECT (project, "Using asset ID %s as URI.", priv->uri);
    } else {
      EmitLoadedInIdle *data = g_new (EmitLoadedInIdle, 1);

      GST_INFO_OBJECT (project, "%s, Loading an empty timeline %s"
          " as no URI set yet", GST_OBJECT_NAME (timeline),
          ges_asset_get_id (GES_ASSET (project)));

      data->timeline = gst_object_ref (timeline);
      data->project = gst_object_ref (project);

      /* Make sure the signal is emitted after the functions ends */
      ges_idle_add ((GSourceFunc) _emit_loaded_in_idle, data, NULL);
      return TRUE;
    }
  }

  GES_PROJECT_LOCK (project);

  if (priv->formatter_asset == NULL)
    priv->formatter_asset = _find_formatter_asset_for_id (priv->uri);

  if (priv->formatter_asset == NULL) {
    lerr = g_error_new (GES_ERROR, 0, "Could not find a suitable formatter");
    goto failed;
  }

  formatter = GES_FORMATTER (ges_asset_extract (priv->formatter_asset, &lerr));
  if (lerr) {
    GST_WARNING_OBJECT (project, "Could not create the formatter: %s",
        lerr->message);

    goto failed;
  }

  uri = g_strdup (priv->uri);

  GES_PROJECT_UNLOCK (project);
  ges_project_add_formatter (GES_PROJECT (project), formatter);
  /* ges_formatter_load_from_uri() might indirectly lead to a
     ges_project_add_asset() call, so do the loading unlocked. */
  ges_formatter_load_from_uri (formatter, timeline, uri, &lerr);
  GES_PROJECT_LOCK (project);

  g_free (uri);

  if (lerr) {
    GST_WARNING_OBJECT (project, "Could not load the timeline,"
        " returning: %s", lerr->message);
    goto failed;
  }

  GES_PROJECT_UNLOCK (project);
  return TRUE;

failed:
  GES_PROJECT_UNLOCK (project);

  if (lerr)
    g_propagate_error (error, lerr);
  return FALSE;
}

static gboolean
_uri_missing_accumulator (GSignalInvocationHint * ihint, GValue * return_accu,
    const GValue * handler_return, gpointer data)
{
  const gchar *ret = g_value_get_string (handler_return);

  if (ret) {
    if (!gst_uri_is_valid (ret)) {
      GST_INFO ("The uri %s was not valid, can not work with it!", ret);
      return TRUE;
    }

    g_value_set_string (return_accu, ret);
    return FALSE;
  }

  return TRUE;
}

/* GESAsset vmethod implementation */
static GESExtractable *
ges_project_extract (GESAsset * project, GError ** error)
{
  GESTimeline *timeline = g_object_new (GES_TYPE_TIMELINE, NULL);

  ges_extractable_set_asset (GES_EXTRACTABLE (timeline), GES_ASSET (project));
  if (_load_project (GES_PROJECT (project), timeline, error))
    return GES_EXTRACTABLE (timeline);

  gst_object_unref (timeline);
  return NULL;
}

static void
_add_media_new_paths_recursing (const gchar * value)
{
  GFileInfo *info;
  GFileEnumerator *fenum;
  GFile *file = g_file_new_for_uri (value);

  if (!(fenum = g_file_enumerate_children (file,
              "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL))) {
    GST_INFO ("%s is not a folder", value);

    goto done;
  }

  GST_INFO ("Adding folder: %s", value);
  g_ptr_array_add (new_paths, g_strdup (value));
  info = g_file_enumerator_next_file (fenum, NULL, NULL);
  while (info != NULL) {
    if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
      GFile *f = g_file_enumerator_get_child (fenum, info);
      gchar *uri = g_file_get_uri (f);

      _add_media_new_paths_recursing (uri);
      gst_object_unref (f);
      g_free (uri);
    }
    g_object_unref (info);
    info = g_file_enumerator_next_file (fenum, NULL, NULL);
  }

done:
  gst_object_unref (file);
  if (fenum)
    gst_object_unref (fenum);
}

gboolean
ges_add_missing_uri_relocation_uri (const gchar * uri, gboolean recurse)
{
  g_return_val_if_fail (gst_uri_is_valid (uri), FALSE);

  if (new_paths == NULL)
    new_paths = g_ptr_array_new_with_free_func (g_free);

  if (recurse)
    _add_media_new_paths_recursing (uri);
  else
    g_ptr_array_add (new_paths, g_strdup (uri));

  return TRUE;
}

static gchar *
ges_missing_uri_default (GESProject * self, GError * error,
    GESAsset * wrong_asset)
{
  guint i;
  const gchar *old_uri = ges_asset_get_id (wrong_asset);
  gchar *new_id = NULL;

  if (ges_asset_request_id_update (wrong_asset, &new_id, error) && new_id) {
    GST_INFO_OBJECT (self, "Returned guessed new ID: %s", new_id);

    return new_id;
  }

  if (new_paths == NULL)
    return NULL;

  if (tried_uris == NULL)
    tried_uris = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (i = 0; i < new_paths->len; i++) {
    gchar *basename, *res;

    basename = g_path_get_basename (old_uri);
    res = g_build_filename (new_paths->pdata[i], basename, NULL);
    g_free (basename);

    if (g_strcmp0 (old_uri, res) == 0) {
      g_hash_table_add (tried_uris, res);
    } else if (g_hash_table_lookup (tried_uris, res)) {
      GST_DEBUG_OBJECT (self, "File already tried: %s", res);
      g_free (res);
    } else {
      g_hash_table_add (tried_uris, g_strdup (res));
      GST_DEBUG_OBJECT (self, "Trying: %s\n", res);
      return res;
    }
  }

  return NULL;
}

gchar *
ges_uri_asset_try_update_id (GError * error, GESAsset * wrong_asset)
{
  return ges_missing_uri_default (NULL, error, wrong_asset);
}

static void
ges_uri_assets_validate_uri (const gchar * nid)
{
  if (tried_uris)
    g_hash_table_remove (tried_uris, nid);
}

/* GObject vmethod implementation */
static void
_dispose (GObject * object)
{
  GESProjectPrivate *priv = GES_PROJECT (object)->priv;

  if (priv->assets)
    g_hash_table_unref (priv->assets);
  if (priv->loading_assets)
    g_hash_table_unref (priv->loading_assets);
  if (priv->loaded_with_error)
    g_hash_table_unref (priv->loaded_with_error);
  if (priv->formatter_asset)
    gst_object_unref (priv->formatter_asset);

  while (priv->formatters)
    ges_project_remove_formatter (GES_PROJECT (object), priv->formatters->data);

  G_OBJECT_CLASS (ges_project_parent_class)->dispose (object);
}

static void
_finalize (GObject * object)
{
  GESProjectPrivate *priv = GES_PROJECT (object)->priv;

  if (priv->uri)
    g_free (priv->uri);
  g_list_free_full (priv->encoding_profiles, g_object_unref);

  G_OBJECT_CLASS (ges_project_parent_class)->finalize (object);
}

static void
_get_property (GESProject * project, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESProjectPrivate *priv = project->priv;

  switch (property_id) {
    case PROP_URI:
      GES_PROJECT_LOCK (project);
      g_value_set_string (value, priv->uri);
      GES_PROJECT_UNLOCK (project);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (project, property_id, pspec);
  }
}

static void
_set_property (GESProject * project, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    case PROP_URI:
      GES_PROJECT_LOCK (project);
      project->priv->uri = g_value_dup_string (value);
      GES_PROJECT_UNLOCK (project);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (project, property_id, pspec);
  }
}

static void
ges_project_class_init (GESProjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  klass->asset_added = NULL;
  klass->missing_uri = ges_missing_uri_default;
  klass->loading_error = NULL;
  klass->asset_removed = NULL;
  object_class->get_property = (GObjectGetPropertyFunc) _get_property;
  object_class->set_property = (GObjectSetPropertyFunc) _set_property;

  /**
   * GESProject::uri:
   *
   * The location of the project to use.
   */
  _properties[PROP_URI] = g_param_spec_string ("uri", "URI",
      "uri of the project", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST, _properties);

  /**
   * GESProject::asset-added:
   * @project: the #GESProject
   * @asset: The #GESAsset that has been added to @project
   */
  _signals[ASSET_ADDED_SIGNAL] =
      g_signal_new ("asset-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, asset_added),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_ASSET);

  /**
   * GESProject::asset-loading:
   * @project: the #GESProject
   * @asset: The #GESAsset that started loading
   *
   * Since: 1.8
   */
  _signals[ASSET_LOADING_SIGNAL] =
      g_signal_new ("asset-loading", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, asset_loading),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_ASSET);

  /**
   * GESProject::asset-removed:
   * @project: the #GESProject
   * @asset: The #GESAsset that has been removed from @project
   */
  _signals[ASSET_REMOVED_SIGNAL] =
      g_signal_new ("asset-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, asset_removed),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_ASSET);

  /**
   * GESProject::loading:
   * @project: the #GESProject that is starting to load a timeline
   * @timeline: The #GESTimeline that started loading
   *
   * Since: 1.18
   */
  _signals[LOADING_SIGNAL] =
      g_signal_new ("loading", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESProjectClass, loading),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_TIMELINE);

  /**
   * GESProject::loaded:
   * @project: the #GESProject that is done loading a timeline.
   * @timeline: The #GESTimeline that completed loading
   */
  _signals[LOADED_SIGNAL] =
      g_signal_new ("loaded", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESProjectClass, loaded),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_TIMELINE);

  /**
   * GESProject::missing-uri:
   * @project: the #GESProject reporting that a file has moved
   * @error: The error that happened
   * @wrong_asset: The asset with the wrong ID, you should us it and its content
   * only to find out what the new location is.
   *
   * ```c
   * static gchar
   * source_moved_cb (GESProject *project, GError *error, GESAsset *asset_with_error)
   * {
   *   return g_strdup ("file:///the/new/uri.ogg");
   * }
   *
   * static int
   * main (int argc, gchar ** argv)
   * {
   *   GESTimeline *timeline;
   *   GESProject *project = ges_project_new ("file:///some/uri.xges");
   *
   *   g_signal_connect (project, "missing-uri", source_moved_cb, NULL);
   *   timeline = ges_asset_extract (GES_ASSET (project));
   * }
   * ```
   *
   * Returns: (transfer full) (nullable): The new URI of @wrong_asset
   */
  _signals[MISSING_URI_SIGNAL] =
      g_signal_new ("missing-uri", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, missing_uri),
      _uri_missing_accumulator, NULL, NULL,
      G_TYPE_STRING, 2, G_TYPE_ERROR, GES_TYPE_ASSET);

  /**
   * GESProject::error-loading-asset:
   * @project: the #GESProject on which a problem happend when creted a #GESAsset
   * @error: The #GError defining the error that occured, might be %NULL
   * @id: The @id of the asset that failed loading
   * @extractable_type: The @extractable_type of the asset that
   * failed loading
   *
   * Informs you that a #GESAsset could not be created. In case of
   * missing GStreamer plugins, the error will be set to #GST_CORE_ERROR
   * #GST_CORE_ERROR_MISSING_PLUGIN
   */
  _signals[ERROR_LOADING_ASSET] =
      g_signal_new ("error-loading-asset", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, loading_error),
      NULL, NULL, NULL,
      G_TYPE_NONE, 3, G_TYPE_ERROR, G_TYPE_STRING, G_TYPE_GTYPE);

  /**
   * GESProject::error-loading:
   * @project: the #GESProject on which a problem happend when creted a #GESAsset
   * @timeline: The timeline that failed loading
   * @error: The #GError defining the error that occured
   *
   * Since: 1.18
   */
  _signals[ERROR_LOADING] =
      g_signal_new ("error-loading", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, GES_TYPE_TIMELINE, G_TYPE_ERROR);

  object_class->dispose = _dispose;
  object_class->finalize = _finalize;

  GES_ASSET_CLASS (klass)->extract = ges_project_extract;
}

static void
ges_project_init (GESProject * project)
{
  GESProjectPrivate *priv = project->priv =
      ges_project_get_instance_private (project);

  priv->uri = NULL;
  priv->formatters = NULL;
  priv->formatter_asset = NULL;
  priv->encoding_profiles = NULL;
  priv->assets = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, gst_object_unref);
  priv->loading_assets = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, gst_object_unref);
  priv->loaded_with_error = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
}

static gchar *
ges_project_internal_extractable_type_id (GType extractable_type,
    const gchar * id)
{
  return g_strdup_printf ("%s:%s", _extractable_type_name (extractable_type),
      id);
}

static gchar *
ges_project_internal_asset_id (GESAsset * asset)
{
  return
      ges_project_internal_extractable_type_id (ges_asset_get_extractable_type
      (asset), ges_asset_get_id (asset));
}

/* Internally takes project mutex */
static void
_send_error_loading_asset (GESProject * project, GESAsset * asset,
    GError * error)
{
  gchar *internal_id = ges_project_internal_asset_id (asset);
  const gchar *id = ges_asset_get_id (asset);

  GST_DEBUG_OBJECT (project, "Sending error loading asset for %s", id);
  GES_PROJECT_LOCK (project);
  g_hash_table_remove (project->priv->loading_assets, internal_id);
  g_hash_table_add (project->priv->loaded_with_error, internal_id);
  GES_PROJECT_UNLOCK (project);
  g_signal_emit (project, _signals[ERROR_LOADING_ASSET], 0, error,
      id, ges_asset_get_extractable_type (asset));
}

/* Internally takes project mutex */
gchar *
ges_project_try_updating_id (GESProject * project, GESAsset * asset,
    GError * error)
{
  gchar *new_id = NULL;
  const gchar *id;
  gchar *internal_id;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);
  g_return_val_if_fail (error, NULL);

  id = ges_asset_get_id (asset);
  GST_DEBUG_OBJECT (project, "Try to proxy %s", id);
  if (ges_asset_request_id_update (asset, &new_id, error) == FALSE) {
    GST_DEBUG_OBJECT (project, "Type: %s can not be proxied for id: %s "
        "and error: %s", g_type_name (G_OBJECT_TYPE (asset)), id,
        error->message);
    _send_error_loading_asset (project, asset, error);
    g_free (new_id);
    return NULL;
  }

  /* Always send the MISSING_URI signal if requesting new ID is possible
   * so that subclasses of GESProject are aware of the missing-uri */
  g_signal_emit (project, _signals[MISSING_URI_SIGNAL], 0, error, asset,
      &new_id);

  if (new_id) {
    GST_DEBUG_OBJECT (project, "new id found: %s", new_id);
    if (!ges_asset_try_proxy (asset, new_id)) {
      g_free (new_id);
      new_id = NULL;
    }
  } else {
    GST_DEBUG_OBJECT (project, "No new id found for %s", id);
  }

  internal_id = ges_project_internal_asset_id (asset);
  GES_PROJECT_LOCK (project);
  g_hash_table_remove (project->priv->loading_assets, internal_id);
  GES_PROJECT_UNLOCK (project);
  g_free (internal_id);

  if (new_id == NULL)
    _send_error_loading_asset (project, asset, error);


  return new_id;
}

static void
new_asset_cb (GESAsset * source, GAsyncResult * res, GESProject * project)
{
  GError *error = NULL;
  gchar *possible_id = NULL;
  GESAsset *asset = ges_asset_request_finish (res, &error);

  if (error) {
    possible_id = ges_project_try_updating_id (project, source, error);
    g_clear_error (&error);

    if (possible_id == NULL)
      return;

    ges_project_create_asset (project, possible_id,
        ges_asset_get_extractable_type (source));

    g_free (possible_id);
    return;
  }

  if (asset) {
    ges_asset_finish_proxy (asset);
    ges_project_add_asset (project, asset);
    gst_object_unref (asset);
  }
}

/**
 * ges_project_set_loaded:
 * @project: The #GESProject from which to emit the "project-loaded" signal
 *
 * Emits the "loaded" signal. This method should be called by sublasses when
 * the project is fully loaded.
 *
 * Returns: %TRUE if the signale could be emitted %FALSE otherwise
 */
gboolean
ges_project_set_loaded (GESProject * project, GESFormatter * formatter,
    GError * error)
{
  if (error) {
    GST_ERROR_OBJECT (project, "Emit project error-loading %s", error->message);
    g_signal_emit (project, _signals[ERROR_LOADING], 0, formatter->timeline,
        error);
  }

  if (!ges_timeline_in_current_thread (formatter->timeline)) {
    GST_INFO_OBJECT (project, "Loaded in a different thread, "
        "not committing timeline");
  } else if (GST_STATE (formatter->timeline) < GST_STATE_PAUSED) {
    timeline_fill_gaps (formatter->timeline);
  } else {
    ges_timeline_commit (formatter->timeline);
  }

  GST_INFO_OBJECT (project, "Emit project loaded");
  g_signal_emit (project, _signals[LOADED_SIGNAL], 0, formatter->timeline);

  /* We are now done with that formatter */
  ges_project_remove_formatter (project, formatter);
  return TRUE;
}

/* Internally takes project mutex */
void
ges_project_add_loading_asset (GESProject * project, GType extractable_type,
    const gchar * id)
{
  GESAsset *asset;

  if ((asset = ges_asset_cache_lookup (extractable_type, id))) {
    GES_PROJECT_LOCK (project);
    if (g_hash_table_insert (project->priv->loading_assets,
            ges_project_internal_asset_id (asset), gst_object_ref (asset))) {
      GES_PROJECT_UNLOCK (project);
      g_signal_emit (project, _signals[ASSET_LOADING_SIGNAL], 0, asset);
    } else {
      GES_PROJECT_UNLOCK (project);
    }
  }
}

/**************************************
 *                                    *
 *         API Implementation         *
 *                                    *
 **************************************/

/**
 * ges_project_create_asset:
 * @project: A #GESProject
 * @id: (allow-none): The id of the asset to create and add to @project
 * @extractable_type: The #GType of the asset to create
 *
 * Create and add a #GESAsset to @project. You should connect to the
 * "asset-added" signal to get the asset when it finally gets added to
 * @project
 *
 * Returns: %TRUE if the asset was added and started loading, %FALSE it was
 * already in the project.
 *
 * MT safe.
 */
gboolean
ges_project_create_asset (GESProject * project, const gchar * id,
    GType extractable_type)
{
  gchar *internal_id;
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      FALSE);

  if (id == NULL)
    id = g_type_name (extractable_type);
  internal_id = ges_project_internal_extractable_type_id (extractable_type, id);

  GES_PROJECT_LOCK (project);
  if (g_hash_table_lookup (project->priv->assets, internal_id) ||
      g_hash_table_lookup (project->priv->loading_assets, internal_id) ||
      g_hash_table_lookup (project->priv->loaded_with_error, internal_id)) {

    GES_PROJECT_UNLOCK (project);
    g_free (internal_id);
    return FALSE;
  }
  GES_PROJECT_UNLOCK (project);
  g_free (internal_id);

  /* TODO Add a GCancellable somewhere in our API */
  ges_asset_request_async (extractable_type, id, NULL,
      (GAsyncReadyCallback) new_asset_cb, project);
  ges_project_add_loading_asset (project, extractable_type, id);

  return TRUE;
}

/**
 * ges_project_create_asset_sync:
 * @project: A #GESProject
 * @id: (allow-none): The id of the asset to create and add to @project
 * @extractable_type: The #GType of the asset to create
 * @error: A #GError to be set in case of error
 *
 * Create and add a #GESAsset to @project. You should connect to the
 * "asset-added" signal to get the asset when it finally gets added to
 * @project
 *
 * Returns: (transfer full) (nullable): The newly created #GESAsset or %NULL.
 *
 * MT safe.
 */
GESAsset *
ges_project_create_asset_sync (GESProject * project, const gchar * id,
    GType extractable_type, GError ** error)
{
  GESAsset *asset;
  gchar *possible_id = NULL, *internal_id;
  gboolean retry = TRUE;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      FALSE);

  if (id == NULL)
    id = g_type_name (extractable_type);

  internal_id = ges_project_internal_extractable_type_id (extractable_type, id);
  GES_PROJECT_LOCK (project);
  if ((asset = g_hash_table_lookup (project->priv->assets, internal_id))) {
    GES_PROJECT_UNLOCK (project);
    g_free (internal_id);

    return gst_object_ref (asset);
  } else if (g_hash_table_lookup (project->priv->loading_assets, internal_id) ||
      g_hash_table_lookup (project->priv->loaded_with_error, internal_id)) {
    GES_PROJECT_UNLOCK (project);
    g_free (internal_id);

    return NULL;
  }
  GES_PROJECT_UNLOCK (project);
  g_free (internal_id);

  /* TODO Add a GCancellable somewhere in our API */
  while (retry) {

    if (g_type_is_a (extractable_type, GES_TYPE_URI_CLIP)) {
      asset = GES_ASSET (ges_uri_clip_asset_request_sync (id, error));
    } else {
      asset = ges_asset_request (extractable_type, id, error);
    }

    if (asset) {
      retry = FALSE;
      internal_id =
          ges_project_internal_extractable_type_id (extractable_type, id);
      GES_PROJECT_LOCK (project);
      if ((!g_hash_table_lookup (project->priv->assets, internal_id))) {
        GES_PROJECT_UNLOCK (project);
        g_signal_emit (project, _signals[ASSET_LOADING_SIGNAL], 0, asset);
      } else {
        GES_PROJECT_UNLOCK (project);
      }

      g_free (internal_id);

      if (possible_id) {
        g_free (possible_id);
        ges_uri_assets_validate_uri (id);
      }

      break;
    } else {
      GESAsset *tmpasset;

      tmpasset = ges_asset_cache_lookup (extractable_type, id);
      possible_id = ges_project_try_updating_id (project, tmpasset, *error);

      if (possible_id == NULL) {
        g_signal_emit (project, _signals[ASSET_LOADING_SIGNAL], 0, tmpasset);
        g_signal_emit (project, _signals[ERROR_LOADING_ASSET], 0, *error, id,
            extractable_type);
        return NULL;
      }


      g_clear_error (error);

      id = possible_id;
    }
  }

  if (!ges_asset_get_proxy_target (asset))
    ges_asset_finish_proxy (asset);

  ges_project_add_asset (project, asset);

  return asset;
}

/**
 * ges_project_add_asset:
 * @project: A #GESProject
 * @asset: (transfer none): A #GESAsset to add to @project
 *
 * Adds a #GESAsset to @project, the project will keep a reference on
 * @asset.
 *
 * Returns: %TRUE if the asset could be added %FALSE it was already
 * in the project.
 *
 * MT safe.
 */
gboolean
ges_project_add_asset (GESProject * project, GESAsset * asset)
{
  gchar *internal_id;
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  GES_PROJECT_LOCK (project);
  internal_id = ges_project_internal_asset_id (asset);
  if (g_hash_table_lookup (project->priv->assets, internal_id)) {
    g_free (internal_id);
    GES_PROJECT_UNLOCK (project);
    return TRUE;
  }

  g_hash_table_insert (project->priv->assets, internal_id,
      gst_object_ref (asset));
  g_hash_table_remove (project->priv->loading_assets, internal_id);
  GES_PROJECT_UNLOCK (project);
  GST_DEBUG_OBJECT (project, "Asset added: %s", ges_asset_get_id (asset));
  g_signal_emit (project, _signals[ASSET_ADDED_SIGNAL], 0, asset);

  return TRUE;
}

/**
 * ges_project_remove_asset:
 * @project: A #GESProject
 * @asset: (transfer none): A #GESAsset to remove from @project
 *
 * Remove @asset from @project.
 *
 * Returns: %TRUE if the asset could be removed %FALSE otherwise
 *
 * MT safe.
 */
gboolean
ges_project_remove_asset (GESProject * project, GESAsset * asset)
{
  gboolean ret;
  gchar *internal_id;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  internal_id = ges_project_internal_asset_id (asset);
  GES_PROJECT_LOCK (project);
  ret = g_hash_table_remove (project->priv->assets, internal_id);
  GES_PROJECT_UNLOCK (project);
  g_free (internal_id);
  g_signal_emit (project, _signals[ASSET_REMOVED_SIGNAL], 0, asset);

  return ret;
}

/**
 * ges_project_get_asset:
 * @project: A #GESProject
 * @id: The id of the asset to retrieve
 * @extractable_type: The extractable_type of the asset
 * to retrieve from @object
 *
 * Returns: (transfer full) (nullable): The #GESAsset with
 * @id or %NULL if no asset with @id as an ID
 *
 * MT safe.
 */
GESAsset *
ges_project_get_asset (GESProject * project, const gchar * id,
    GType extractable_type)
{
  GESAsset *asset;
  gchar *internal_id;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      NULL);

  internal_id = ges_project_internal_extractable_type_id (extractable_type, id);
  GES_PROJECT_LOCK (project);
  asset = g_hash_table_lookup (project->priv->assets, internal_id);
  GES_PROJECT_UNLOCK (project);
  g_free (internal_id);

  if (asset)
    return gst_object_ref (asset);

  return NULL;
}

/**
 * ges_project_list_assets:
 * @project: A #GESProject
 * @filter: Type of assets to list, `GES_TYPE_EXTRACTABLE` will list
 * all assets
 *
 * List all @asset contained in @project filtering per extractable_type
 * as defined by @filter. It copies the asset and thus will not be updated
 * in time.
 *
 * Returns: (transfer full) (element-type GESAsset): The list of
 * #GESAsset the object contains
 *
 * MT safe.
 */
GList *
ges_project_list_assets (GESProject * project, GType filter)
{
  GList *ret = NULL;
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);
  g_return_val_if_fail (filter == G_TYPE_NONE
      || g_type_is_a (filter, GES_TYPE_EXTRACTABLE), NULL);

  GES_PROJECT_LOCK (project);
  g_hash_table_iter_init (&iter, project->priv->assets);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (g_type_is_a (ges_asset_get_extractable_type (GES_ASSET (value)),
            filter))
      ret = g_list_append (ret, gst_object_ref (value));
  }
  GES_PROJECT_UNLOCK (project);

  return ret;
}

/**
 * ges_project_save:
 * @project: A #GESProject to save
 * @timeline: The #GESTimeline to save, it must have been extracted from @project
 * @uri: The uri where to save @project and @timeline
 * @formatter_asset: (transfer full) (allow-none): The formatter asset to
 * use or %NULL. If %NULL, will try to save in the same format as the one
 * from which the timeline as been loaded or default to the best formatter
 * as defined in #ges_find_formatter_for_uri
 * @overwrite: %TRUE to overwrite file if it exists
 * @error: An error to be set in case something wrong happens or %NULL
 *
 * Save the timeline of @project to @uri. You should make sure that @timeline
 * is one of the timelines that have been extracted from @project
 * (using ges_asset_extract (@project);)
 *
 * Returns: %TRUE if the project could be save, %FALSE otherwise
 *
 * MT safe.
 */
gboolean
ges_project_save (GESProject * project, GESTimeline * timeline,
    const gchar * uri, GESAsset * formatter_asset, gboolean overwrite,
    GError ** error)
{
  GESAsset *tl_asset;
  gboolean ret = TRUE;
  GESFormatter *formatter = NULL;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (formatter_asset == NULL ||
      g_type_is_a (ges_asset_get_extractable_type (formatter_asset),
          GES_TYPE_FORMATTER), FALSE);
  g_return_val_if_fail ((error == NULL || *error == NULL), FALSE);

  GES_PROJECT_LOCK (project);

  tl_asset = ges_extractable_get_asset (GES_EXTRACTABLE (timeline));
  if (tl_asset == NULL && project->priv->uri == NULL) {
    GESAsset *asset = ges_asset_cache_lookup (GES_TYPE_PROJECT, uri);

    if (asset) {
      GST_WARNING_OBJECT (project, "Trying to save project to %s but we already"
          "have %" GST_PTR_FORMAT " for that uri, can not save", uri, asset);
      goto out;
    }

    GST_DEBUG_OBJECT (project, "Timeline %" GST_PTR_FORMAT " has no asset"
        " we have no uri set, so setting ourself as asset", timeline);

    ges_extractable_set_asset (GES_EXTRACTABLE (timeline), GES_ASSET (project));
  } else if (tl_asset != GES_ASSET (project)) {
    GST_WARNING_OBJECT (project, "Timeline %" GST_PTR_FORMAT
        " not created by this project can not save", timeline);

    ret = FALSE;
    goto out;
  }

  if (formatter_asset == NULL) {
    formatter_asset = gst_object_ref (ges_find_formatter_for_uri (uri));
  }

  formatter = GES_FORMATTER (ges_asset_extract (formatter_asset, error));
  if (formatter == NULL) {
    GST_WARNING_OBJECT (project, "Could not create the formatter %p %s: %s",
        formatter_asset, ges_asset_get_id (formatter_asset),
        (error && *error) ? (*error)->message : "Unknown Error");

    ret = FALSE;
    goto out;
  }

  GES_PROJECT_UNLOCK (project);
  ges_project_add_formatter (project, formatter);
  ret = ges_formatter_save_to_uri (formatter, timeline, uri, overwrite, error);
  if (ret && project->priv->uri == NULL)
    ges_project_set_uri (project, uri);

  GES_PROJECT_LOCK (project);

out:
  GES_PROJECT_UNLOCK (project);

  if (formatter_asset)
    gst_object_unref (formatter_asset);
  ges_project_remove_formatter (project, formatter);

  return ret;
}

/**
 * ges_project_new:
 * @uri: (allow-none): The uri to be set after creating the project.
 *
 * Creates a new #GESProject and sets its uri to @uri if provided. Note that
 * if @uri is not valid or %NULL, the uri of the project will then be set
 * the first time you save the project. If you then save the project to
 * other locations, it will never be updated again and the first valid URI is
 * the URI it will keep refering to.
 *
 * Returns: A newly created #GESProject
 *
 * MT safe.
 */
GESProject *
ges_project_new (const gchar * uri)
{
  gchar *id = (gchar *) uri;
  GESProject *project;

  if (uri == NULL)
    id = g_strdup_printf ("project-%i", nb_projects++);

  project = GES_PROJECT (ges_asset_request (GES_TYPE_TIMELINE, id, NULL));

  if (project && uri)
    ges_project_set_uri (project, uri);

  if (uri == NULL)
    g_free (id);

  return project;
}

/**
 * ges_project_load:
 * @project: A #GESProject that has an @uri set already
 * @timeline: A blank timeline to load @project into
 * @error: An error to be set in case something wrong happens or %NULL
 *
 * Loads @project into @timeline
 *
 * Returns: %TRUE if the project could be loaded %FALSE otherwise.
 *
 * MT safe.
 */
gboolean
ges_project_load (GESProject * project, GESTimeline * timeline, GError ** error)
{
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (project->priv->uri, FALSE);
  g_return_val_if_fail (timeline->tracks == NULL, FALSE);

  if (!_load_project (project, timeline, error))
    return FALSE;

  ges_extractable_set_asset (GES_EXTRACTABLE (timeline), GES_ASSET (project));

  return TRUE;
}

/**
 * ges_project_get_uri:
 * @project: A #GESProject
 *
 * Retrieve the uri that is currently set on @project
 *
 * Returns: (transfer full) (nullable): a newly allocated string representing uri.
 *
 * MT safe.
 */
gchar *
ges_project_get_uri (GESProject * project)
{
  gchar *uri = NULL;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  GES_PROJECT_LOCK (project);
  if (project->priv->uri)
    uri = g_strdup (project->priv->uri);
  GES_PROJECT_UNLOCK (project);

  return uri;
}

/**
 * ges_project_add_encoding_profile:
 * @project: A #GESProject
 * @profile: A #GstEncodingProfile to add to the project. If a profile with
 * the same name already exists, it will be replaced.
 *
 * Adds @profile to the project. It lets you save in what format
 * the project will be rendered and keep a reference to those formats.
 * Also, those formats will be saved to the project file when possible.
 *
 * Returns: %TRUE if @profile could be added, %FALSE otherwise
 *
 * MT safe.
 */
gboolean
ges_project_add_encoding_profile (GESProject * project,
    GstEncodingProfile * profile)
{
  GList *tmp;
  GESProjectPrivate *priv;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (GST_IS_ENCODING_PROFILE (profile), FALSE);

  GES_PROJECT_LOCK (project);
  priv = project->priv;
  for (tmp = priv->encoding_profiles; tmp; tmp = tmp->next) {
    GstEncodingProfile *tmpprofile = GST_ENCODING_PROFILE (tmp->data);

    if (g_strcmp0 (gst_encoding_profile_get_name (tmpprofile),
            gst_encoding_profile_get_name (profile)) == 0) {
      GST_INFO_OBJECT (project, "Already have profile: %s, replacing it",
          gst_encoding_profile_get_name (profile));

      gst_object_unref (tmp->data);
      tmp->data = gst_object_ref (profile);
      GES_PROJECT_UNLOCK (project);
      return TRUE;
    }
  }

  priv->encoding_profiles = g_list_prepend (priv->encoding_profiles,
      gst_object_ref (profile));
  GES_PROJECT_UNLOCK (project);
  return TRUE;
}

/**
 * ges_project_list_encoding_profiles:
 * @project: A #GESProject
 *
 * Lists the encoding profile that have been set to @project. The first one
 * is the latest added.
 *
 * Returns: (transfer none) (element-type GstPbutils.EncodingProfile) (allow-none): The
 * list of #GstEncodingProfile used in @project
 */
const GList *
ges_project_list_encoding_profiles (GESProject * project)
{
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  return project->priv->encoding_profiles;
}

/**
 * ges_project_get_loading_assets:
 * @project: A #GESProject
 *
 * Get the assets that are being loaded
 *
 * Returns: (transfer full) (element-type GES.Asset): A set of loading asset
 * that will be added to @project. Note that those Asset are *not* loaded yet,
 * and thus can not be used.
 *
 * MT safe.
 */
GList *
ges_project_get_loading_assets (GESProject * project)
{
  GHashTableIter iter;
  gpointer key, value;

  GList *ret = NULL;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);

  GES_PROJECT_LOCK (project);
  g_hash_table_iter_init (&iter, project->priv->loading_assets);
  while (g_hash_table_iter_next (&iter, &key, &value))
    ret = g_list_prepend (ret, gst_object_ref (value));

  GES_PROJECT_UNLOCK (project);
  return ret;
}
