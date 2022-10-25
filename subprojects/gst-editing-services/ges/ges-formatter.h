/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#pragma once

#include <glib-object.h>
#include <ges/ges-timeline.h>

G_BEGIN_DECLS

#define GES_TYPE_FORMATTER ges_formatter_get_type()
GES_DECLARE_TYPE(Formatter, formatter, FORMATTER);

/**
 * GESFormatter:
 *
 * Base class for timeline data serialization and deserialization.
 */

struct _GESFormatter
{
  GInitiallyUnowned parent;

  /*< private >*/
  GESFormatterPrivate *priv;

  /*< protected >*/
  GESProject *project;
  GESTimeline *timeline;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

typedef gboolean (*GESFormatterCanLoadURIMethod) (GESFormatter *dummy_instance, const gchar * uri, GError **error);

/**
 * GESFormatterLoadFromURIMethod:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: the URI to load from
 * @error: An error to be set in case something wrong happens or %NULL
 *
 * Virtual method for loading a timeline from a given URI.
 *
 * Every #GESFormatter subclass needs to implement this method.
 *
 * Returns: TRUE if the @timeline was properly loaded from the given @uri,
 * else FALSE.
 **/
typedef gboolean (*GESFormatterLoadFromURIMethod) (GESFormatter *formatter,
                  GESTimeline *timeline,
                  const gchar * uri,
                  GError **error);

/**
 * GESFormatterSaveToURIMethod:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: the URI to save to
 * @overwrite: Whether the file should be overwritten in case it exists
 * @error: An error to be set in case something wrong happens or %NULL
 *
 * Virtual method for saving a timeline to a uri.
 *
 * Every #GESFormatter subclass needs to implement this method.
 *
 * Returns: TRUE if the @timeline was properly stored to the given @uri,
 * else FALSE.
 */
typedef gboolean (*GESFormatterSaveToURIMethod) (GESFormatter *formatter,
               GESTimeline *timeline, const gchar * uri, gboolean overwrite,
               GError **error);

/**
 * GESFormatterClass:
 * @parent_class: the parent class structure
 * @can_load_uri: Whether the URI can be loaded
 * @load_from_uri: class method to deserialize data from a URI
 * @save_to_uri: class method to serialize data to a URI
 *
 * GES Formatter class. Override the vmethods to implement the formatter functionnality.
 */

struct _GESFormatterClass {
  GInitiallyUnownedClass parent_class;

  /* TODO 2.0: Rename the loading method to can_load and load.
   * Technically we just pass data to load, it should not necessarily
   * be a URI */
  GESFormatterCanLoadURIMethod can_load_uri;
  GESFormatterLoadFromURIMethod load_from_uri;
  GESFormatterSaveToURIMethod save_to_uri;

  /* < private > */
  gchar *name;
  gchar *description;
  gchar *extension;
  gchar *mimetype;
  gdouble version;
  GstRank rank;


  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
void ges_formatter_class_register_metas (GESFormatterClass * klass,
                                         const gchar *name,
                                         const gchar *description,
                                         const gchar *extensions,
                                         const gchar *caps,
                                         gdouble version,
                                         GstRank rank);

GES_API
gboolean ges_formatter_can_load_uri     (const gchar * uri, GError **error);
GES_API
gboolean ges_formatter_can_save_uri     (const gchar * uri, GError **error);

GES_DEPRECATED_FOR(ges_timeline_load_from_uri)
gboolean ges_formatter_load_from_uri    (GESFormatter * formatter,
                                         GESTimeline  *timeline,
                                         const gchar *uri,
                                         GError **error);

GES_DEPRECATED_FOR(ges_timeline_save_to_uri)
gboolean ges_formatter_save_to_uri      (GESFormatter * formatter,
                                         GESTimeline *timeline,
                                         const gchar *uri,
                                         gboolean overwrite,
                                         GError **error);

GES_API
GESAsset *ges_formatter_get_default    (void);

GES_API
GESAsset *ges_find_formatter_for_uri   (const gchar *uri);

G_END_DECLS
