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

typedef struct _GESFormatterPrivate GESFormatterPrivate;

/**
 * GESFormatter:
 *
 * Base class for timeline data serialization and deserialization.
 */

struct _GESFormatter {
  GObject parent;

  /*< private >*/
  GESFormatterPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

typedef gboolean (*GESFormatterCanLoadURIMethod) (const gchar * uri);
typedef gboolean (*GESFormatterCanSaveURIMethod) (const gchar * uri);

/**
 * GESFormatterLoadFromURIMethod:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: the URI to load from
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
						     const gchar * uri);

/**
 * GESFormatterSaveToURIMethod:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: the URI to save to
 *
 * Virtual method for saving a timeline to a uri.
 *
 * Every #GESFormatter subclass needs to implement this method.
 *
 * Returns: TRUE if the @timeline was properly stored to the given @uri,
 * else FALSE.
 */
typedef gboolean (*GESFormatterSaveToURIMethod) (GESFormatter *formatter,
						   GESTimeline *timeline,
						   const gchar * uri);
typedef gboolean (*GESFormatterSaveMethod) (GESFormatter * formatter,
					      GESTimeline * timeline);
typedef gboolean (*GESFormatterLoadMethod) (GESFormatter * formatter,
					      GESTimeline * timeline);

/**
 * GESFormatterSourceMovedMethod:
 * @formatter: a #GESFormatter
 * @tfs: a #GESTimelineFileSource
 * @new_uri: the new URI of @tfs
 *
 * Virtual method for changing the URI of a #GESTimelineFileSource that has been
 * moved between the saving and the loading of the timeline.
 *
 * This virtual method is not 100% necessary to be implemented as it is an
 * extra feature.
 *
 * Returns: %TRUE if the source URI could be modified properly, %FALSE otherwize.
 */
typedef gboolean (*GESFormatterSourceMovedMethod)        (GESFormatter *formatter,
					   GESTimelineFileSource *tfs, gchar *new_uri);

/**
 * GESFormatterClass:
 * @parent_class: the parent class structure
 * @can_load_uri: Whether the URI can be loaded
 * @can_save_uri: Whether the URI can be saved
 * @load_from_uri: class method to deserialize data from a URI
 * @save_to_uri: class method to serialize data to a URI
 * @update_source_uri: virtual method to specify that a source has moved, and thus its URI
 * must be set to its new location (specified by the user)
 *
 * GES Formatter class. Override the vmethods to implement the formatter functionnality.
 */

struct _GESFormatterClass {
  GObjectClass parent_class;

  GESFormatterCanLoadURIMethod can_load_uri;
  GESFormatterCanSaveURIMethod can_save_uri;
  GESFormatterLoadFromURIMethod load_from_uri;
  GESFormatterSaveToURIMethod save_to_uri;
  GESFormatterSourceMovedMethod update_source_uri;

  /*< private >*/
  /* FIXME : formatter name */
  /* FIXME : formatter description */
  /* FIXME : format name/mime-type */

  GESFormatterSaveMethod save;
  GESFormatterLoadMethod load;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_formatter_get_type (void);

/* Main Formatter methods */
GESFormatter *ges_formatter_new_for_uri (const gchar *uri);
GESFormatter *ges_default_formatter_new (void);

gboolean ges_formatter_can_load_uri     (const gchar * uri);
gboolean ges_formatter_can_save_uri     (const gchar * uri);

gboolean ges_formatter_load_from_uri    (GESFormatter * formatter,
					 GESTimeline  *timeline,
					 const gchar *uri);

gboolean ges_formatter_save_to_uri      (GESFormatter * formatter,
					 GESTimeline *timeline,
					 const gchar *uri);

gboolean
ges_formatter_update_source_uri         (GESFormatter * formatter,
    GESTimelineFileSource * source, gchar * new_uri);

/* Non-standard methods. WILL BE DEPRECATED */
gboolean ges_formatter_load             (GESFormatter * formatter,
					 GESTimeline * timeline);
gboolean ges_formatter_save             (GESFormatter * formatter,
					 GESTimeline * timeline);

void ges_formatter_set_data             (GESFormatter * formatter,
					 void *data, gsize length);
void *ges_formatter_get_data            (GESFormatter *formatter,
					 gsize *length);
void ges_formatter_clear_data           (GESFormatter *formatter);


#endif /* _GES_FORMATTER */
