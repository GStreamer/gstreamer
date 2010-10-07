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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GES_FORMATTER
#define _GES_FORMATTER

#include <glib-object.h>
#include <ges/ges-timeline.h>

#define GES_TYPE_FORMATTER ges_formatter_get_type()

#define GES_FORMATTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_FORMATTER, GESFormatter))

#define GES_FORMATTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_FORMATTER, GESFormatterClass))

#define GES_IS_FORMATTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_FORMATTER))

#define GES_IS_FORMATTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_FORMATTER))

#define GES_FORMATTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_FORMATTER, GESFormatterClass))

/**
 * GESFormatter:
 *
 * Base class for timeline data serialization and deserialization.
 */

struct _GESFormatter {
  GObject parent;

  /*< private >*/
  gchar    *data;
  gsize    length;
};

/**
 * GESFormatterClass:
 * @parent_class: parent class
 * @can_load_uri: class method which returns true if a #GESFormatterClass can read
 * from a given URI. 
 * @can_save_uri: class method which rturns true of a #GEFormatterClass can
 * write to a given URI.
 * @load_from_uri: class method to deserialize data from a URI
 * @save_from_uri: class method to serialize data to a URI
 * @save: method to save timeline data
 * @load: method to load timeline data
 * 
 */

struct _GESFormatterClass {
  GObjectClass parent_class;

  gboolean (*can_load_uri) (gchar * uri);
  gboolean (*can_save_uri) (gchar * uri);
  gboolean (*load_from_uri) (GESFormatter *, GESTimeline *, gchar * uri);
  gboolean (*save_to_uri) (GESFormatter *, GESTimeline *, gchar * uri);
  gboolean (*save) (GESFormatter * formatter, GESTimeline * timeline);
  gboolean (*load) (GESFormatter * formatter, GESTimeline * timeline);
};

GType ges_formatter_get_type (void);

GESFormatter *ges_formatter_new_for_uri (gchar *uri);
GESFormatter *ges_default_formatter_new (void);

gboolean ges_formatter_can_load_uri (gchar * uri);
gboolean ges_formatter_can_save_uri (gchar * uri);

gboolean ges_formatter_load_from_uri (GESFormatter * formatter, GESTimeline
    *timeline, gchar *uri);

gboolean ges_formatter_save_to_uri (GESFormatter * formatter, GESTimeline *timeline,
    gchar *uri);

void ges_formatter_set_data (GESFormatter * formatter, void *data, gsize
    length);

void *ges_formatter_get_data (GESFormatter *formatter, gsize *length);
void ges_formatter_clear_data (GESFormatter *formatter);

gboolean ges_formatter_load (GESFormatter * formatter, GESTimeline * timeline);
gboolean ges_formatter_save (GESFormatter * formatter, GESTimeline * timeline);

#endif /* _GES_FORMATTER */
