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
#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>
#include <gst/pbutils/encoding-profile.h>

G_BEGIN_DECLS

#define GES_TYPE_PROJECT            ges_project_get_type()
GES_DECLARE_TYPE(Project, project, PROJECT);

struct _GESProject
{
  GESAsset parent;

  /* <private> */
  GESProjectPrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING_LARGE];
};

struct _GESProjectClass
{
  GESAssetClass parent_class;

  /* Signals */
  void     (*asset_added)    (GESProject * self,
                              GESAsset   * asset);
  void     (*asset_loading)  (GESProject * self,
                              GESAsset   * asset);
  void     (*asset_removed)  (GESProject * self,
                              GESAsset   * asset);
  gchar *  (*missing_uri)    (GESProject * self,
                              GError     * error,
                              GESAsset   * wrong_asset);
  gboolean (*loading_error)  (GESProject * self,
                              GError     * error,
                              gchar      * id,
                              GType extractable_type);
  gboolean (*loaded)         (GESProject  * self,
                              GESTimeline * timeline);
  /**
   * GESProjectClass::loading:
   * @self: The self
   * @timeline: The loading timeline
   *
   * Since: 1.18
   */
  void (*loading)           (GESProject  * self,
                             GESTimeline * timeline);

  gpointer _ges_reserved[GES_PADDING - 1];
};

GES_API
gboolean  ges_project_add_asset    (GESProject* project,
                                    GESAsset *asset);
GES_API
gboolean  ges_project_remove_asset (GESProject *project,
                                    GESAsset * asset);
GES_API
GList   * ges_project_list_assets  (GESProject * project,
                                    GType filter) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gboolean  ges_project_save         (GESProject * project,
                                    GESTimeline * timeline,
                                    const gchar *uri,
                                    GESAsset * formatter_asset,
                                    gboolean overwrite,
                                    GError **error);
GES_API
gboolean  ges_project_load         (GESProject * project,
                                    GESTimeline * timeline,
                                    GError **error);
GES_API
GESProject * ges_project_new       (const gchar *uri) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gchar      * ges_project_get_uri   (GESProject *project) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GESAsset   * ges_project_get_asset (GESProject * project,
                                    const gchar *id,
                                    GType extractable_type) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gboolean ges_project_create_asset  (GESProject * project,
                                    const gchar *id,
                                    GType extractable_type);

GES_API
GESAsset * ges_project_create_asset_sync        (GESProject * project,
                                                 const gchar * id,
                                                 GType extractable_type,
                                                 GError **error) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GList * ges_project_get_loading_assets          (GESProject * project) G_GNUC_WARN_UNUSED_RESULT;

GES_API
gboolean ges_project_add_encoding_profile       (GESProject *project,
                                                 GstEncodingProfile *profile);
GES_API
const GList *ges_project_list_encoding_profiles (GESProject *project);
GES_API
gboolean ges_add_missing_uri_relocation_uri    (const gchar * uri,
                                                gboolean recurse);
GES_API
void ges_project_add_formatter (GESProject * project, GESFormatter * formatter);

G_END_DECLS
