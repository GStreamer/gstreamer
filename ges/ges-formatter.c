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

/**
 * SECTION:ges-formatter
 * @short_description: Timeline saving and loading.
 *
 * The #GESFormatter is the object responsible for loading and/or saving the contents
 * of a #GESTimeline to/from various formats.
 *
 * In order to save a #GESTimeline, you can either let GES pick a default formatter by
 * using ges_timeline_save_to_uri(), or pick your own formatter and use
 * ges_formatter_save_to_uri().
 *
 * To load a #GESTimeline, you might want to be able to track the progress of the loading,
 * in which case you should create an empty #GESTimeline, connect to the relevant signals
 * and call ges_formatter_load_from_uri().
 *
 * If you do not care about tracking the loading progress, you can use the convenience
 * ges_timeline_new_from_uri() method.
 *
 * Support for saving or loading new formats can be added by creating a subclass of
 * #GESFormatter and implement the various vmethods of #GESFormatterClass.
 *
 * Note that subclasses should call ges_formatter_project_loaded when they are done
 * loading a project.
 **/

#include <gst/gst.h>
#include <gio/gio.h>
#include <stdlib.h>

#include "ges-formatter.h"
#include "ges-internal.h"
#include "ges.h"

static void ges_extractable_interface_init (GESExtractableInterface * iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESFormatter, ges_formatter,
    G_TYPE_INITIALLY_UNOWNED, G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

struct _GESFormatterPrivate
{
  gpointer nothing;
};

static void ges_formatter_dispose (GObject * object);
static gboolean load_from_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri, GError ** error);
static gboolean save_to_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri, GError ** error);
static gboolean default_can_load_uri (const gchar * uri, GError ** error);
static gboolean default_can_save_uri (const gchar * uri, GError ** error);

enum
{
  LAST_SIGNAL
};

/* Utils */
static GESFormatterClass *
ges_formatter_find_for_uri (const gchar * uri)
{
  GType *formatters;
  guint n_formatters, i;
  GESFormatterClass *class, *ret = NULL;

  formatters = g_type_children (GES_TYPE_FORMATTER, &n_formatters);
  for (i = 0; i < n_formatters; i++) {
    class = g_type_class_ref (formatters[i]);

    if (class->can_load_uri (uri, NULL)) {
      ret = class;
      break;
    }
    g_type_class_unref (class);
  }

  g_free (formatters);

  return ret;
}

/* GESExtractable implementation */
static gchar *
extractable_check_id (GType type, const gchar * id)
{
  if (gst_uri_is_valid (id))
    return g_strdup (id);

  return NULL;
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  GESAsset *asset;

  if (!(asset = ges_extractable_get_asset (self)))
    return NULL;

  return g_strdup (ges_asset_get_id (asset));
}

static GType
extractable_get_real_extractable_type (GType type, const gchar * id)
{
  GType real_type = G_TYPE_NONE;
  GESFormatterClass *class;

  class = ges_formatter_find_for_uri (id);
  if (class) {
    real_type = G_OBJECT_CLASS_TYPE (class);
    g_type_class_unref (class);
  }

  return real_type;
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_id = extractable_get_id;
  iface->get_real_extractable_type = extractable_get_real_extractable_type;
}

static void
ges_formatter_class_init (GESFormatterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESFormatterPrivate));

  object_class->dispose = ges_formatter_dispose;

  klass->can_load_uri = default_can_load_uri;
  klass->can_save_uri = default_can_save_uri;
  klass->load_from_uri = load_from_uri;
  klass->save_to_uri = save_to_uri;
}

static void
ges_formatter_init (GESFormatter * object)
{
}

static void
ges_formatter_dispose (GObject * object)
{


}

static gboolean
default_can_load_uri (const gchar * uri, GError ** error)
{
  GST_ERROR ("No 'can_load_uri' vmethod implementation");
  return FALSE;
}

static gboolean
default_can_save_uri (const gchar * uri, GError ** error)
{
  GST_ERROR ("No 'can_save_uri' vmethod implementation");
  return FALSE;
}

/**
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to the URI
 * @error: A #GError that will be set in case of error
 *
 * Checks if there is a #GESFormatter available which can load a #GESTimeline
 * from the given URI.
 *
 * Returns: TRUE if there is a #GESFormatter that can support the given uri
 * or FALSE if not.
 */

gboolean
ges_formatter_can_load_uri (const gchar * uri, GError ** error)
{
  if (!(gst_uri_is_valid (uri))) {
    GST_ERROR ("Invalid uri!");
    return FALSE;
  }

  if (!(gst_uri_has_protocol (uri, "file"))) {
    gchar *proto = gst_uri_get_protocol (uri);
    GST_ERROR ("Unspported protocol '%s'", proto);
    g_free (proto);
    return FALSE;
  }

  /* FIXME Reimplement */
  GST_FIXME ("This should be reimplemented");

  return FALSE;
}

/**
 * ges_formatter_can_save_uri:
 * @uri: a #gchar * pointing to a URI
 * @error: A #GError that will be set in case of error
 *
 * Returns TRUE if there is a #GESFormatter available which can save a
 * #GESTimeline to the given URI.
 *
 * Returns: TRUE if the given @uri is supported, else FALSE.
 */

gboolean
ges_formatter_can_save_uri (const gchar * uri, GError ** error)
{
  if (!(gst_uri_is_valid (uri))) {
    GST_ERROR ("%s invalid uri!", uri);
    return FALSE;
  }

  if (!(gst_uri_has_protocol (uri, "file"))) {
    gchar *proto = gst_uri_get_protocol (uri);
    GST_ERROR ("Unspported protocol '%s'", proto);
    g_free (proto);
    return FALSE;
  }

  /* TODO: implement file format registry */
  /* TODO: search through the registry and chose a GESFormatter class that can
   * handle the URI.*/

  return TRUE;
}

/**
 * ges_formatter_load_from_uri:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * @error: A #GError that will be set in case of error
 *
 * Load data from the given URI into timeline.
 *
 * Returns: TRUE if the timeline data was successfully loaded from the URI,
 * else FALSE.
 */

gboolean
ges_formatter_load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri, GError ** error)
{
  gboolean ret = FALSE;
  GESFormatterClass *klass = GES_FORMATTER_GET_CLASS (formatter);

  g_return_val_if_fail (GES_IS_FORMATTER (formatter), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  if (klass->load_from_uri) {
    ges_timeline_enable_update (timeline, FALSE);
    formatter->timeline = timeline;
    ret = klass->load_from_uri (formatter, timeline, uri, error);
    ges_timeline_enable_update (timeline, TRUE);
  }

  return ret;
}

static gboolean
load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri, GError ** error)
{
  GST_FIXME ("This should be reimplemented");
  return FALSE;
}

/**
 * ges_formatter_save_to_uri:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * @error: A #GError that will be set in case of error
 *
 * Save data from timeline to the given URI.
 *
 * Returns: TRUE if the timeline data was successfully saved to the URI
 * else FALSE.
 */

gboolean
ges_formatter_save_to_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri, GError ** error)
{
  GESFormatterClass *klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->save_to_uri)
    return klass->save_to_uri (formatter, timeline, uri, error);

  GST_ERROR ("not implemented!");

  return FALSE;
}

static gboolean
save_to_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri, GError ** error)
{
  GST_FIXME ("This should be reimplemented");
  return FALSE;
}
