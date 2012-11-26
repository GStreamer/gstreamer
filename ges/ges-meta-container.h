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

#ifndef _GES_METADATA_CONTAINER
#define _GES_METADATA_CONTAINER

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_METADATA_CONTAINER                 (ges_meta_container_get_type ())
#define GES_METADATA_CONTAINER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_METADATA_CONTAINER, GESMetadataContainer))
#define GES_IS_METADATA_CONTAINER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_METADATA_CONTAINER))
#define GES_METADATA_CONTAINER_GET_INTERFACE (inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GES_TYPE_METADATA_CONTAINER, GESMetadataContainerInterface))

typedef struct _GESMetadataContainer          GESMetadataContainer;
typedef struct _GESMetadataContainerInterface GESMetadataContainerInterface;

struct _GESMetadataContainerInterface {
  GTypeInterface parent_iface;

  gpointer _ges_reserved[GES_PADDING];
};

GType ges_meta_container_get_type (void);

void
ges_meta_container_set_boolean     (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gboolean value);

void
ges_meta_container_set_int         (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gint value);

void
ges_meta_container_set_uint        (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        guint value);

void
ges_meta_container_set_int64       (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gint64 value);

void
ges_meta_container_set_uint64      (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        guint64 value);

void
ges_meta_container_set_float       (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gfloat value);

void
ges_meta_container_set_double      (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gdouble value);

void
ges_meta_container_set_date        (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        const GDate* value);

void
ges_meta_container_set_date_time   (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        const GstDateTime* value);

void
ges_meta_container_set_string      (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        const gchar* value);

void
ges_meta_container_set_meta            (GESMetadataContainer * container,
                                        const gchar* metadata_item,
                                        const GValue *value);

gboolean
ges_meta_container_get_boolean     (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gboolean* dest);

gboolean
ges_meta_container_get_int         (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gint* dest);

gboolean
ges_meta_container_get_uint        (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        guint* dest);

gboolean
ges_meta_container_get_int64       (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gint64* dest);

gboolean
ges_meta_container_get_uint64      (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        guint64* dest);

gboolean
ges_meta_container_get_float       (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gfloat* dest);

gboolean
ges_meta_container_get_double      (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        gdouble* dest);

gboolean
ges_meta_container_get_date        (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        GDate** dest);

gboolean
ges_meta_container_get_date_time   (GESMetadataContainer *container,
                                        const gchar* metadata_item,
                                        GstDateTime** dest);

gboolean
ges_meta_container_get_string      (GESMetadataContainer * container,
                                        const gchar * metadata_item,
                                        gchar** dest);

gboolean
ges_meta_container_get_meta            (GESMetadataContainer * container,
                                        const gchar* metadata_item,
                                        GValue* value);

typedef void
(*GESMetadataForeachFunc)              (const GESMetadataContainer *container,
                                        const gchar *key,
                                        GValue *value,
                                        gpointer user_data);

void
ges_metadata_container_foreach         (GESMetadataContainer *container,
                                        GESMetadataForeachFunc func,
                                        gpointer user_data);

gchar *
ges_metadata_container_to_string       (GESMetadataContainer *container);

GESMetadataContainer *
ges_metadata_container_new_from_string (const gchar *str);

void
ges_metadata_register                  (const gchar *name,
                                        GType type);

G_END_DECLS
#endif /* _GES_METADATA_CONTAINER */
