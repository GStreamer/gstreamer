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
 **/

#include <gst/gst.h>
#include <stdlib.h>
#include "ges-formatter.h"
#include "ges-keyfile-formatter.h"
#include "ges-internal.h"

G_DEFINE_ABSTRACT_TYPE (GESFormatter, ges_formatter, G_TYPE_OBJECT);

struct _GESFormatterPrivate
{
  gchar *data;
  gsize length;
};

static void ges_formatter_dispose (GObject * object);
static gboolean load_from_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri);
static gboolean save_to_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri);
static gboolean default_can_load_uri (const gchar * uri);
static gboolean default_can_save_uri (const gchar * uri);

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
  object->priv = G_TYPE_INSTANCE_GET_PRIVATE (object,
      GES_TYPE_FORMATTER, GESFormatterPrivate);
}

static void
ges_formatter_dispose (GObject * object)
{
  GESFormatterPrivate *priv = GES_FORMATTER (object)->priv;

  if (priv->data) {
    g_free (priv->data);
  }
}

/**
 * ges_formatter_new_for_uri:
 * @uri: a #gchar * pointing to the uri
 *
 * Creates a #GESFormatter that can handle the given URI.
 *
 * Returns: A GESFormatter that can load the given uri, or NULL if
 * the uri is not supported.
 */

GESFormatter *
ges_formatter_new_for_uri (const gchar * uri)
{
  if (ges_formatter_can_load_uri (uri))
    return GES_FORMATTER (ges_keyfile_formatter_new ());
  return NULL;
}

/**
 * ges_default_formatter_new:
 *
 * Creates a new instance of the default GESFormatter type on this system
 * (currently #GESKeyfileFormatter).
 *
 * Returns: (transfer full): a #GESFormatter instance or %NULL
 */

GESFormatter *
ges_default_formatter_new (void)
{
  return GES_FORMATTER (ges_keyfile_formatter_new ());
}

static gboolean
default_can_load_uri (const gchar * uri)
{
  GST_ERROR ("No 'can_load_uri' vmethod implementation");
  return FALSE;
}

static gboolean
default_can_save_uri (const gchar * uri)
{
  GST_ERROR ("No 'can_save_uri' vmethod implementation");
  return FALSE;
}

/**
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to the URI
 * 
 * Checks if there is a #GESFormatter available which can load a #GESTimeline
 * from the given URI.
 *
 * Returns: TRUE if there is a #GESFormatter that can support the given uri
 * or FALSE if not.
 */

gboolean
ges_formatter_can_load_uri (const gchar * uri)
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

  /* TODO: implement file format registry */
  /* TODO: search through the registry and chose a GESFormatter class that can
   * handle the URI.*/

  return TRUE;
}

/**
 * ges_formatter_can_save_uri:
 * @uri: a #gchar * pointing to a URI
 * 
 * Returns TRUE if there is a #GESFormatter available which can save a
 * #GESTimeline to the given URI.
 *
 * Returns: TRUE if the given @uri is supported, else FALSE.
 */

gboolean
ges_formatter_can_save_uri (const gchar * uri)
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

  /* TODO: implement file format registry */
  /* TODO: search through the registry and chose a GESFormatter class that can
   * handle the URI.*/

  return TRUE;
}

/**
 * ges_formatter_set_data:
 * @formatter: a #GESFormatter
 * @data: the data to be set on the formatter
 * @length: the length of the data in bytes
 *
 * Set the data that this formatter will use for loading. The formatter will
 * takes ownership of the data and will free the data if
 * @ges_formatter_set_data is called again or when the formatter itself is
 * disposed. You should call @ges_formatter_clear_data () if you do not wish
 * this to happen.
 */

void
ges_formatter_set_data (GESFormatter * formatter, void *data, gsize length)
{
  GESFormatterPrivate *priv = GES_FORMATTER (formatter)->priv;

  if (priv->data)
    g_free (priv->data);
  priv->data = data;
  priv->length = length;
}

/**
 * ges_formatter_get_data:
 * @formatter: a #GESFormatter
 * @length: location into which to store the size of the data in bytes.
 *
 * Lets you get the data @formatter used for loading.
 *
 * Returns: (transfer none): a pointer to the data.
 */
void *
ges_formatter_get_data (GESFormatter * formatter, gsize * length)
{
  GESFormatterPrivate *priv = GES_FORMATTER (formatter)->priv;

  *length = priv->length;

  return priv->data;
}

/**
 * ges_formatter_clear_data:
 * @formatter: a #GESFormatter
 *
 * clears the data from a #GESFormatter without freeing it. You should call
 * this before disposing or setting data on a #GESFormatter if the current data
 * pointer should not be freed.
 */

void
ges_formatter_clear_data (GESFormatter * formatter)
{
  GESFormatterPrivate *priv = GES_FORMATTER (formatter)->priv;

  priv->data = NULL;
  priv->length = 0;
}

/**
 * ges_formatter_load:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 *
 * Loads data from formatter to into timeline. You should first call
 * ges_formatter_set_data() with the location and size of a block of data
 * from which to read.
 *
 * Returns: TRUE if the data was successfully loaded into timeline
 * or FALSE if an error occured during loading.
 */

gboolean
ges_formatter_load (GESFormatter * formatter, GESTimeline * timeline)
{
  GESFormatterClass *klass;

  klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->load)
    return klass->load (formatter, timeline);
  GST_ERROR ("not implemented!");
  return FALSE;
}

/**
 * ges_formatter_save:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 *
 * Save data from timeline into a block of data. You can retrieve the location
 * and size of this data with ges_formatter_get_data().
 *
 * Returns: TRUE if the timeline data was successfully saved for FALSE if
 * an error occured during saving.
 */

gboolean
ges_formatter_save (GESFormatter * formatter, GESTimeline * timeline)
{
  GESFormatterClass *klass;
  GList *layers;

  /* Saving an empty timeline is not allowed */
  /* FIXME : Having a ges_timeline_is_empty() would be more efficient maybe */
  layers = ges_timeline_get_layers (timeline);

  g_return_val_if_fail (layers != NULL, FALSE);
  g_list_foreach (layers, (GFunc) g_object_unref, NULL);
  g_list_free (layers);

  klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->save)
    return klass->save (formatter, timeline);

  GST_ERROR ("not implemented!");

  return FALSE;
}

/**
 * ges_formatter_load_from_uri:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * 
 * Load data from the given URI into timeline.
 *
 * Returns: TRUE if the timeline data was successfully loaded from the URI,
 * else FALSE.
 */

gboolean
ges_formatter_load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri)
{
  GESFormatterClass *klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->load_from_uri)
    return klass->load_from_uri (formatter, timeline, uri);

  return FALSE;
}

static gboolean
load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri)
{
  gchar *location;
  GError *e = NULL;
  gboolean ret = TRUE;
  GESFormatterPrivate *priv = GES_FORMATTER (formatter)->priv;


  if (priv->data) {
    GST_ERROR ("formatter already has data! please set data to NULL");
  }

  if (!(location = gst_uri_get_location (uri))) {
    return FALSE;
  }

  if (g_file_get_contents (location, &priv->data, &priv->length, &e)) {
    if (!ges_formatter_load (formatter, timeline)) {
      GST_ERROR ("couldn't deserialize formatter");
      ret = FALSE;
    }
  } else {
    GST_ERROR ("couldn't read file '%s': %s", location, e->message);
    ret = FALSE;
  }

  if (e)
    g_error_free (e);
  g_free (location);

  return ret;
}

/**
 * ges_formatter_save_to_uri:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 *
 * Save data from timeline to the given URI.
 *
 * Returns: TRUE if the timeline data was successfully saved to the URI
 * else FALSE.
 */

gboolean
ges_formatter_save_to_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri)
{
  GESFormatterClass *klass = GES_FORMATTER_GET_CLASS (formatter);
  GList *layers;

  /* Saving an empty timeline is not allowed */
  /* FIXME : Having a ges_timeline_is_empty() would be more efficient maybe */
  layers = ges_timeline_get_layers (timeline);

  g_return_val_if_fail (layers != NULL, FALSE);
  g_list_foreach (layers, (GFunc) g_object_unref, NULL);
  g_list_free (layers);

  if (klass->save_to_uri)
    return klass->save_to_uri (formatter, timeline, uri);

  GST_ERROR ("not implemented!");

  return FALSE;
}

static gboolean
save_to_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri)
{
  gchar *location;
  GError *e = NULL;
  gboolean ret = TRUE;
  GESFormatterPrivate *priv = GES_FORMATTER (formatter)->priv;

  if (!(location = g_filename_from_uri (uri, NULL, NULL))) {
    return FALSE;
  }

  if (!ges_formatter_save (formatter, timeline)) {
    GST_ERROR ("couldn't serialize formatter");
  } else {
    if (!g_file_set_contents (location, priv->data, priv->length, &e)) {
      GST_ERROR ("couldn't write file '%s': %s", location, e->message);
      ret = FALSE;
    }
  }

  if (e)
    g_error_free (e);
  g_free (location);

  return ret;
}
