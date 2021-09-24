/* GStreamer Editing Services
 * Copyright (C) 2012 Paul Lange <palango@gmx.de>
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
#include <gst/gst.h>
#include <ges/ges-types.h>
#include "ges-enums.h"

G_BEGIN_DECLS

#define GES_TYPE_META_CONTAINER                 (ges_meta_container_get_type ())
#define GES_META_CONTAINER_GET_INTERFACE (inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GES_TYPE_META_CONTAINER, GESMetaContainerInterface))
GES_API
G_DECLARE_INTERFACE(GESMetaContainer, ges_meta_container, GES, META_CONTAINER, GObject);

/**
 * GES_META_FORMATTER_NAME:
 *
 * The name of a formatter, used as the #GESAsset:id for #GESFormatter
 * assets (string).
 */
#define GES_META_FORMATTER_NAME                       "name"

/**
 * GES_META_DESCRIPTION:
 *
 * The description of the object, to be used in various contexts (string).
 */
#define GES_META_DESCRIPTION                         "description"

/**
 * GES_META_FORMATTER_MIMETYPE:
 *
 * The mimetype used for the file produced by a #GESFormatter (string).
 */
#define GES_META_FORMATTER_MIMETYPE                   "mimetype"

/**
 * GES_META_FORMATTER_EXTENSION:
 *
 * The file extension of files produced by a #GESFormatter (string).
 */
#define GES_META_FORMATTER_EXTENSION                  "extension"

/**
 * GES_META_FORMATTER_VERSION:
 *
 * The version of a #GESFormatter (double).
 */
#define GES_META_FORMATTER_VERSION                    "version"

/**
 * GES_META_FORMATTER_RANK:
 *
 * The rank of a #GESFormatter (a #GstRank).
 */
#define GES_META_FORMATTER_RANK                       "rank"

/**
 * GES_META_VOLUME:
 *
 * The volume for a #GESTrack or a #GESLayer (float).
 */
#define GES_META_VOLUME                              "volume"

/**
 * GES_META_VOLUME_DEFAULT:
 *
 * The default volume for a #GESTrack or a #GESLayer as a float.
 */
#define GES_META_VOLUME_DEFAULT                       1.0

/**
 * GES_META_FORMAT_VERSION:
 *
 * The version of the format in which a project is serialized (string).
 */
#define GES_META_FORMAT_VERSION                       "format-version"

/**
 * GES_META_MARKER_COLOR:
 *
 * The ARGB color of a #GESMarker (an AARRGGBB hex as a uint).
 */
#define GES_META_MARKER_COLOR                         "marker-color"

typedef struct _GESMetaContainer          GESMetaContainer;
typedef struct _GESMetaContainerInterface GESMetaContainerInterface;

struct _GESMetaContainerInterface {
  GTypeInterface parent_iface;

  gpointer _ges_reserved[GES_PADDING];
};

GES_API gboolean
ges_meta_container_set_boolean     (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gboolean value);

GES_API gboolean
ges_meta_container_set_int         (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gint value);

GES_API gboolean
ges_meta_container_set_uint        (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        guint value);

GES_API gboolean
ges_meta_container_set_int64       (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gint64 value);

GES_API gboolean
ges_meta_container_set_uint64      (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        guint64 value);

GES_API gboolean
ges_meta_container_set_float       (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gfloat value);

GES_API gboolean
ges_meta_container_set_double      (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gdouble value);

GES_API gboolean
ges_meta_container_set_date        (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        const GDate* value);

GES_API gboolean
ges_meta_container_set_date_time   (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        const GstDateTime* value);

GES_API gboolean
ges_meta_container_set_string      (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        const gchar* value);

GES_API gboolean
ges_meta_container_set_meta            (GESMetaContainer * container,
                                        const gchar* meta_item,
                                        const GValue *value);

GES_API gboolean
ges_meta_container_set_marker_list     (GESMetaContainer * container,
                                        const gchar * meta_item,
                                        const GESMarkerList *list);

GES_API gboolean
ges_meta_container_register_static_meta (GESMetaContainer * container,
                                         GESMetaFlag flags,
                                         const gchar * meta_item,
                                         GType type);

GES_API gboolean
ges_meta_container_register_meta_boolean (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          gboolean value);

GES_API gboolean
ges_meta_container_register_meta_int     (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          gint value);

GES_API gboolean
ges_meta_container_register_meta_uint    (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          guint value);

GES_API gboolean
ges_meta_container_register_meta_int64   (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          gint64 value);

GES_API gboolean
ges_meta_container_register_meta_uint64  (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          guint64 value);

GES_API gboolean
ges_meta_container_register_meta_float   (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          gfloat value);

GES_API gboolean
ges_meta_container_register_meta_double  (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          gdouble value);

GES_API gboolean
ges_meta_container_register_meta_date    (GESMetaContainer *container,
                                          GESMetaFlag flags,
                                          const gchar* meta_item,
                                          const GDate* value);

GES_API gboolean
ges_meta_container_register_meta_date_time  (GESMetaContainer *container,
                                             GESMetaFlag flags,
                                             const gchar* meta_item,
                                             const GstDateTime* value);

GES_API gboolean
ges_meta_container_register_meta_string     (GESMetaContainer *container,
                                             GESMetaFlag flags,
                                             const gchar* meta_item,
                                             const gchar* value);

GES_API gboolean
ges_meta_container_register_meta            (GESMetaContainer *container,
                                             GESMetaFlag flags,
                                             const gchar* meta_item,
                                             const GValue * value);

GES_API gboolean
ges_meta_container_check_meta_registered    (GESMetaContainer *container,
                                             const gchar * meta_item,
                                             GESMetaFlag * flags,
                                             GType * type);

GES_API gboolean
ges_meta_container_get_boolean     (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gboolean* dest);

GES_API gboolean
ges_meta_container_get_int         (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gint* dest);

GES_API gboolean
ges_meta_container_get_uint        (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        guint* dest);

GES_API gboolean
ges_meta_container_get_int64       (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gint64* dest);

GES_API gboolean
ges_meta_container_get_uint64      (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        guint64* dest);

GES_API gboolean
ges_meta_container_get_float       (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gfloat* dest);

GES_API gboolean
ges_meta_container_get_double      (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        gdouble* dest);

GES_API gboolean
ges_meta_container_get_date        (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        GDate** dest);

GES_API gboolean
ges_meta_container_get_date_time   (GESMetaContainer *container,
                                        const gchar* meta_item,
                                        GstDateTime** dest);

GES_API const gchar *
ges_meta_container_get_string      (GESMetaContainer * container,
                                        const gchar * meta_item);

GES_API GESMarkerList *
ges_meta_container_get_marker_list (GESMetaContainer * container,
                                    const gchar * key);

GES_API const GValue *
ges_meta_container_get_meta            (GESMetaContainer * container,
                                        const gchar * key);
/**
 * GESMetaForeachFunc:
 * @container: A #GESMetaContainer
 * @key: The key for one of @container's fields
 * @value: The set value under @key
 * @user_data: User data
 *
 * A method to be called on all of a meta container's fields.
 */
typedef void
(*GESMetaForeachFunc)                  (const GESMetaContainer *container,
                                        const gchar *key,
                                        const GValue *value,
                                        gpointer user_data);

GES_API void
ges_meta_container_foreach             (GESMetaContainer *container,
                                        GESMetaForeachFunc func,
                                        gpointer user_data);

GES_API gchar *
ges_meta_container_metas_to_string     (GESMetaContainer *container);

GES_API gboolean
ges_meta_container_add_metas_from_string (GESMetaContainer *container,
                                          const gchar *str);

G_END_DECLS
