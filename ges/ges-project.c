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
 * @short_description: A GESAsset that is used to manage projects
 *
 * The #GESProject is used to control a set of #GESAsset and is a
 * #GESAsset with #GES_TYPE_TIMELINE as @extractable_type itself. That
 * means that you can extract #GESTimeline from a project as followed:
 *
 * |[
 *  GESProject *project;
 *  GESTimeline *timeline;
 *
 *  project = ges_project_new ("file:///path/to/a/valid/project/uri");
 *
 *  // Here you can connect to the various signal to get more infos about
 *  // what is happening and recover from errors if possible
 *  ...
 *
 *  timeline = ges_asset_extract (GES_ASSET (project));
 * ]|
 *
 * The #GESProject class offers a higher level API to handle #GESAsset-s.
 * It lets you request new asset, and it informs you about new assets through
 * a set of signals. Also it handles problem such as missing files/missing
 * #GstElement and lets you try to recover from those.
 */
#include "ges.h"
#include "ges-internal.h"

/* TODO We should rely on both extractable_type and @id to identify
 * a Asset, not only @id
 */
G_DEFINE_TYPE (GESProject, ges_project, GES_TYPE_ASSET);

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
};

typedef struct EmitLoadedInIdle
{
  GESProject *project;
  GESTimeline *timeline;
} EmitLoadedInIdle;

enum
{
  LOADED_SIGNAL,
  ERROR_LOADING_ASSET,
  ASSET_ADDED_SIGNAL,
  ASSET_REMOVED_SIGNAL,
  MISSING_URI_SIGNAL,
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

static guint nb_projects = 0;

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
  ges_timeline_commit (data->timeline);
  g_signal_emit (data->project, _signals[LOADED_SIGNAL], 0, data->timeline);

  gst_object_unref (data->project);
  gst_object_unref (data->timeline);
  g_slice_free (EmitLoadedInIdle, data);

  return FALSE;
}

static void
ges_project_add_formatter (GESProject * project, GESFormatter * formatter)
{
  GESProjectPrivate *priv = GES_PROJECT (project)->priv;

  ges_formatter_set_project (formatter, project);
  priv->formatters = g_list_append (priv->formatters, formatter);

  gst_object_ref_sink (formatter);
}

static void
ges_project_remove_formatter (GESProject * project, GESFormatter * formatter)
{
  GList *tmp;
  GESProjectPrivate *priv = GES_PROJECT (project)->priv;

  for (tmp = priv->formatters; tmp; tmp = tmp->next) {
    if (tmp->data == formatter) {
      gst_object_unref (formatter);
      priv->formatters = g_list_delete_link (priv->formatters, tmp);

      return;
    }
  }
}

static void
ges_project_set_uri (GESProject * project, const gchar * uri)
{
  GESProjectPrivate *priv;

  g_return_if_fail (GES_IS_PROJECT (project));

  priv = project->priv;
  if (priv->uri) {
    GST_WARNING_OBJECT (project, "Trying to rest URI, this is prohibited");

    return;
  }

  if (uri == NULL || !gst_uri_is_valid (uri)) {
    GST_LOG_OBJECT (project, "Invalid URI: %s", uri);
    return;
  }

  priv->uri = g_strdup (uri);

  /* We use that URI as ID */
  ges_asset_set_id (GES_ASSET (project), uri);

  return;
}

static gboolean
_load_project (GESProject * project, GESTimeline * timeline, GError ** error)
{
  GError *lerr = NULL;
  GESProjectPrivate *priv;
  GESFormatter *formatter;

  priv = GES_PROJECT (project)->priv;

  if (priv->uri == NULL) {
    EmitLoadedInIdle *data = g_slice_new (EmitLoadedInIdle);

    GST_LOG_OBJECT (project, "%s, Loading an empty timeline %s"
        " as no URI set yet", GST_OBJECT_NAME (timeline),
        ges_asset_get_id (GES_ASSET (project)));

    data->timeline = gst_object_ref (timeline);
    data->project = gst_object_ref (project);

    /* Make sure the signal is emitted after the functions ends */
    g_idle_add ((GSourceFunc) _emit_loaded_in_idle, data);
    return TRUE;
  }

  if (priv->formatter_asset == NULL)
    priv->formatter_asset = _find_formatter_asset_for_uri (priv->uri);

  if (priv->formatter_asset == NULL)
    goto failed;

  formatter = GES_FORMATTER (ges_asset_extract (priv->formatter_asset, &lerr));
  if (lerr) {
    GST_WARNING_OBJECT (project, "Could not create the formatter: %s",
        (*error)->message);

    goto failed;
  }

  ges_project_add_formatter (GES_PROJECT (project), formatter);
  ges_formatter_load_from_uri (formatter, timeline, priv->uri, &lerr);
  if (lerr) {
    GST_WARNING_OBJECT (project, "Could not load the timeline,"
        " returning: %s", lerr->message);
    goto failed;
  }

  return TRUE;

failed:
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

/* GObject vmethod implementation */
static void
_dispose (GObject * object)
{
  GList *tmp;
  GESProjectPrivate *priv = GES_PROJECT (object)->priv;

  if (priv->assets)
    g_hash_table_unref (priv->assets);
  if (priv->loading_assets)
    g_hash_table_unref (priv->loading_assets);
  if (priv->loaded_with_error)
    g_hash_table_unref (priv->loaded_with_error);
  if (priv->formatter_asset)
    gst_object_unref (priv->formatter_asset);

  for (tmp = priv->formatters; tmp; tmp = tmp->next)
    ges_project_remove_formatter (GES_PROJECT (object), tmp->data);;

  G_OBJECT_CLASS (ges_project_parent_class)->dispose (object);
}

static void
_finalize (GObject * object)
{
  GESProjectPrivate *priv = GES_PROJECT (object)->priv;

  if (priv->uri)
    g_free (priv->uri);

  G_OBJECT_CLASS (ges_project_parent_class)->finalize (object);
}

static void
_get_property (GESProject * project, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESProjectPrivate *priv = project->priv;

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, priv->uri);
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
      project->priv->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (project, property_id, pspec);
  }
}

static void
ges_project_class_init (GESProjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESProjectPrivate));

  klass->asset_added = NULL;
  klass->missing_uri = NULL;
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
   * @formatter: the #GESProject
   * @asset: The #GESAsset that has been added to @project
   */
  _signals[ASSET_ADDED_SIGNAL] =
      g_signal_new ("asset-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, asset_added),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_ASSET);

  /**
   * GESProject::asset-removed:
   * @formatter: the #GESProject
   * @asset: The #GESAsset that has been removed from @project
   */
  _signals[ASSET_REMOVED_SIGNAL] =
      g_signal_new ("asset-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, asset_removed),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_ASSET);

  /**
   * GESProject::loaded:
   * @project: the #GESProject that is done loading a project.
   * @timeline: The #GESTimeline that complete loading
   */
  _signals[LOADED_SIGNAL] =
      g_signal_new ("loaded", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESProjectClass, loaded),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      1, GES_TYPE_TIMELINE);

  /**
   * GESProject::missing-uri:
   * @project: the #GESProject reporting that a file has moved
   * @error: The error that happened
   * @wrong_asset: The asset with the wrong ID, you should us it and its content
   * only to find out what the new location is.
   *
   * |[
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
   * ]|
   *
   * Returns: (transfer full) (allow-none): The new URI of @wrong_asset
   */
  _signals[MISSING_URI_SIGNAL] =
      g_signal_new ("missing-uri", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GESProjectClass, missing_uri),
      _uri_missing_accumulator, NULL, g_cclosure_marshal_generic,
      G_TYPE_STRING, 2, G_TYPE_ERROR, GES_TYPE_ASSET);

  /**
   * GESProject::error-loading-asset:
   * @project: the #GESProject on which a problem happend when creted a #GESAsset
   * @error: The #GError defining the error that accured, might be %NULL
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
      NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 3, G_TYPE_ERROR, G_TYPE_STRING, G_TYPE_GTYPE);

  object_class->dispose = _dispose;
  object_class->finalize = _finalize;

  GES_ASSET_CLASS (klass)->extract = ges_project_extract;
}

static void
ges_project_init (GESProject * project)
{
  GESProjectPrivate *priv = project->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (project,
      GES_TYPE_PROJECT, GESProjectPrivate);

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

static void
_send_error_loading_asset (GESProject * project, GESAsset * asset,
    GError * error)
{
  const gchar *id = ges_asset_get_id (asset);

  GST_DEBUG_OBJECT (project, "Sending error loading asset for %s", id);
  g_hash_table_remove (project->priv->loading_assets, id);
  g_hash_table_add (project->priv->loaded_with_error, g_strdup (id));
  g_signal_emit (project, _signals[ERROR_LOADING_ASSET], 0, error, id,
      ges_asset_get_extractable_type (asset));
}

gchar *
ges_project_try_updating_id (GESProject * project, GESAsset * asset,
    GError * error)
{
  gchar *new_id = NULL;
  const gchar *id;

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

    return NULL;
  }

  if (new_id == NULL) {
    GST_DEBUG_OBJECT (project, "Sending 'missing-uri' signal for %s", id);
    g_signal_emit (project, _signals[MISSING_URI_SIGNAL], 0, error, asset,
        &new_id);
  }

  if (new_id) {
    GST_DEBUG_OBJECT (project, "new id found: %s", new_id);
    if (!ges_asset_set_proxy (asset, new_id)) {
      g_free (new_id);
      new_id = NULL;
    }
  } else {
    GST_DEBUG_OBJECT (project, "No new id found for %s", id);
  }

  g_hash_table_remove (project->priv->loading_assets, id);

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

    if (possible_id == NULL)
      return;

    ges_project_create_asset (project, possible_id,
        ges_asset_get_extractable_type (source));

    g_free (possible_id);
    g_error_free (error);
    return;
  }

  ges_project_add_asset (project, asset);
  if (asset)
    gst_object_unref (asset);
}

/**
 * ges_project_set_loaded:
 * @project: The #GESProject from which to emit the "project-loaded" signal
 *
 * Emits the "loaded" signal. This method should be called by sublasses when
 * the project is fully loaded.
 *
 * Returns: %TRUE if the signale could be emitted %FALSE otherwize
 */
gboolean
ges_project_set_loaded (GESProject * project, GESFormatter * formatter)
{
  GST_INFO_OBJECT (project, "Emit project loaded");
  ges_timeline_commit (formatter->timeline);
  g_signal_emit (project, _signals[LOADED_SIGNAL], 0, formatter->timeline);

  /* We are now done with that formatter */
  ges_project_remove_formatter (project, formatter);
  return TRUE;
}

void
ges_project_add_loading_asset (GESProject * project, GType extractable_type,
    const gchar * id)
{
  GESAsset *asset;

  if ((asset = ges_asset_cache_lookup (extractable_type, id)))
    g_hash_table_insert (project->priv->loading_assets, g_strdup (id),
        gst_object_ref (asset));
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
 * Returns: %TRUE if the asset started to be added %FALSE it was already
 * in the project
 */
gboolean
ges_project_create_asset (GESProject * project, const gchar * id,
    GType extractable_type)
{
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      FALSE);

  if (id == NULL)
    id = g_type_name (extractable_type);

  if (g_hash_table_lookup (project->priv->assets, id) ||
      g_hash_table_lookup (project->priv->loading_assets, id) ||
      g_hash_table_lookup (project->priv->loaded_with_error, id))
    return FALSE;

  /* TODO Add a GCancellable somewhere in our API */
  ges_asset_request_async (extractable_type, id, NULL,
      (GAsyncReadyCallback) new_asset_cb, project);
  ges_project_add_loading_asset (project, extractable_type, id);

  return TRUE;
}

/**
 * ges_project_add_asset:
 * @project: A #GESProject
 * @asset: (transfer none): A #GESAsset to add to @project
 *
 * Adds a #Asset to @project, the project will keep a reference on
 * @asset.
 *
 * Returns: %TRUE if the asset could be added %FALSE it was already
 * in the project
 */
gboolean
ges_project_add_asset (GESProject * project, GESAsset * asset)
{
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  if (g_hash_table_lookup (project->priv->assets, ges_asset_get_id (asset)))
    return FALSE;

  g_hash_table_insert (project->priv->assets,
      g_strdup (ges_asset_get_id (asset)), gst_object_ref (asset));

  g_hash_table_remove (project->priv->loading_assets, ges_asset_get_id (asset));
  GST_DEBUG_OBJECT (project, "Asset added: %s", ges_asset_get_id (asset));
  g_signal_emit (project, _signals[ASSET_ADDED_SIGNAL], 0, asset);

  return TRUE;
}

/**
 * ges_project_remove_asset:
 * @project: A #GESProject
 * @asset: (transfer none): A #GESAsset to remove from @project
 *
 * remove a @asset to from @project.
 *
 * Returns: %TRUE if the asset could be removed %FALSE otherwise
 */
gboolean
ges_project_remove_asset (GESProject * project, GESAsset * asset)
{
  gboolean ret;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  ret = g_hash_table_remove (project->priv->assets, ges_asset_get_id (asset));
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
 * Returns: (transfer full) (allow-none): The #GESAsset with
 * @id or %NULL if no asset with @id as an ID
 */
GESAsset *
ges_project_get_asset (GESProject * project, const gchar * id,
    GType extractable_type)
{
  GESAsset *asset;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      NULL);

  asset = g_hash_table_lookup (project->priv->assets, id);

  if (asset)
    return gst_object_ref (asset);

  return NULL;
}

/**
 * ges_project_list_assets:
 * @project: A #GESProject
 * @filter: Type of assets to list, #GES_TYPE_EXTRACTABLE will list
 * all assets
 *
 * List all @asset contained in @project filtering per extractable_type
 * as defined by @filter. It copies the asset and thus will not be updated
 * in time.
 *
 * Returns: (transfer full) (element-type GESAsset): The list of
 * #GESAsset the object contains
 */
GList *
ges_project_list_assets (GESProject * project, GType filter)
{
  GList *ret = NULL;
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);

  g_hash_table_iter_init (&iter, project->priv->assets);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (g_type_is_a (ges_asset_get_extractable_type (GES_ASSET (value)),
            filter))
      ret = g_list_append (ret, gst_object_ref (value));
  }

  return ret;
}

/**
 * ges_project_save:
 * @project: A #GESProject to save
 * @timeline: The #GESTimeline to save, it must have been extracted from @project
 * @uri: The uri where to save @project and @timeline
 * @formatter_asset: (allow-none): The formatter asset to use or %NULL. If %NULL,
 * will try to save in the same format as the one from which the timeline as been loaded
 * or default to the formatter with highest rank
 * @overwrite: %TRUE to overwrite file if it exists
 * @error: (out) (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Save the timeline of @project to @uri. You should make sure that @timeline
 * is one of the timelines that have been extracted from @project
 * (using ges_asset_extract (@project);)
 *
 * Returns: %TRUE if the project could be save, %FALSE otherwize
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

  if (formatter_asset == NULL)
    formatter_asset = gst_object_ref (ges_formatter_get_default ());

  formatter = GES_FORMATTER (ges_asset_extract (formatter_asset, error));
  if (formatter == NULL) {
    GST_WARNING_OBJECT (project, "Could not create the formatter %p %s: %s",
        formatter_asset, ges_asset_get_id (formatter_asset),
        (error && *error) ? (*error)->message : "Unknown Error");

    ret = FALSE;
    goto out;
  }

  ges_project_add_formatter (project, formatter);
  ret = ges_formatter_save_to_uri (formatter, timeline, uri, overwrite, error);
  if (ret && project->priv->uri == NULL)
    ges_project_set_uri (project, uri);

out:
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

  return project;
}

/**
 * ges_project_load:
 * @project: A #GESProject that has an @uri set already
 * @timeline: A blank timeline to load @project into
 * @error: (out) (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Loads @project into @timeline
 *
 * Returns: %TRUE if the project could be loaded %FALSE otherwize.
 */
gboolean
ges_project_load (GESProject * project, GESTimeline * timeline, GError ** error)
{
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (ges_project_get_uri (project), FALSE);
  g_return_val_if_fail (
      (ges_extractable_get_asset (GES_EXTRACTABLE (timeline)) == NULL), FALSE);

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
 * Returns: The uri that is set on @project
 */
gchar *
ges_project_get_uri (GESProject * project)
{
  GESProjectPrivate *priv;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);

  priv = project->priv;
  if (priv->uri)
    return g_strdup (priv->uri);
  return NULL;
}

/**
 * ges_project_add_encoding_profile:
 * @project: A #GESProject
 * @profile: A #GstEncodingProfile to add to the project. If a profile with
 * the same name already exists, it will be replaced
 *
 * Adds @profile to the project. It lets you save in what format
 * the project has been renders and keep a reference to those formats.
 * Also, those formats will be saves to the project file when possible.
 *
 * Returns: %TRUE if @profile could be added, %FALSE otherwize
 */
gboolean
ges_project_add_encoding_profile (GESProject * project,
    GstEncodingProfile * profile)
{
  GList *tmp;
  GESProjectPrivate *priv;

  g_return_val_if_fail (GES_IS_PROJECT (project), FALSE);
  g_return_val_if_fail (GST_IS_ENCODING_PROFILE (profile), FALSE);

  priv = project->priv;
  for (tmp = priv->encoding_profiles; tmp; tmp = tmp->next) {
    GstEncodingProfile *tmpprofile = GST_ENCODING_PROFILE (tmp->data);

    if (g_strcmp0 (gst_encoding_profile_get_name (tmpprofile),
            gst_encoding_profile_get_name (profile)) == 0) {
      GST_INFO_OBJECT (project, "Already have profile: %s, replacing it",
          gst_encoding_profile_get_name (profile));

      gst_object_unref (tmp->data);
      tmp->data = gst_object_ref (profile);
      return TRUE;
    }
  }

  priv->encoding_profiles = g_list_prepend (priv->encoding_profiles,
      gst_object_ref (profile));

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
 * and thus can not be used
 */
GList *
ges_project_get_loading_assets (GESProject * project)
{
  GHashTableIter iter;
  gpointer key, value;

  GList *ret = NULL;

  g_return_val_if_fail (GES_IS_PROJECT (project), NULL);

  g_hash_table_iter_init (&iter, project->priv->loading_assets);
  while (g_hash_table_iter_next (&iter, &key, &value))
    ret = g_list_prepend (ret, gst_object_ref (value));

  return ret;
}
