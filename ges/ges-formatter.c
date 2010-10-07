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
 * @short_description: Base Class for loading and saving #GESTimeline data.
 *
 * Responsible for loading and/or saving the contents of a #GESTimeline to/from
 * various formats.
 **/

#include <gst/gst.h>
#include <stdlib.h>
#include "ges-formatter.h"
#include "ges.h"
#include "ges-internal.h"

G_DEFINE_TYPE (GESFormatter, ges_formatter, G_TYPE_OBJECT);

static void ges_formatter_dispose (GObject * object);
static void ges_formatter_finalize (GObject * object);

static void
ges_formatter_class_init (GESFormatterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ges_formatter_dispose;
  object_class->finalize = ges_formatter_finalize;
}

static void
ges_formatter_init (GESFormatter * object)
{
}

static void
ges_formatter_dispose (GObject * object)
{
  GESFormatter *formatter;
  formatter = GES_FORMATTER (object);

  if (formatter->data) {
    g_free (formatter->data);
  }
}

static void
ges_formatter_finalize (GObject * formatter)
{
}

/**
 * ges_formatter_new_for_uri:
 * @uri: a #gchar * pointing to the uri
 *
 * Creates a #GESFormatter that can handle the given URI.
 *
 * Returns: A GESFormatter or subclass that can load the given uri, or NULL if
 * the uri is not supported.
 */

GESFormatter *
ges_formatter_new_for_uri (gchar * uri)
{
  if (ges_formatter_can_load_uri (uri))
    return GES_FORMATTER (ges_keyfile_formatter_new ());
  return NULL;
}

/**
 * ges_formatter_default_new:
 *
 * Creates a new instance of the default GESFormatter type on this system
 * (currently #GESKeyFileFormatter).
 *
 * Returns: a #GESFormatter instance or NULL 
 */

GESFormatter *
ges_default_formatter_new (void)
{
  return GES_FORMATTER (ges_keyfile_formatter_new ());
}

/**
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to the URI
 * 
 * Returns true if there is a #GESFormatterClass derivative registered with
 * the system which can load data from the given URI.
 *
 * Returns: TRUE if the given uri is supported or FALSE if the uri is 
 * not supported.
 */

gboolean
ges_formatter_can_load_uri (gchar * uri)
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
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to a URI
 * 
 * Returns TRUE if thereis a #GESFormatterClass derivative registered with the
 * system which can save data to the given URI.
 *
 * Returns: TRUE if the given uri is supported or FALSE if the given URI is
 * not suported.
 */

gboolean
ges_formatter_can_save_uri (gchar * uri)
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
  return FALSE;
}

/**
 * ges_formatter_load:
 * @formatter: a pointer to a #GESFormatter instance or subclass.
 * @timeline: a pointer to a #GESTimeline
 *
 * Loads data from formatter to into timeline. The data field of formatter
 * must point to a block of data, and length must be correctly set to the
 * length of the data. This method is only implemented in subclasses.
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
 * @formatter: a pointer to a #GESFormatter instance or subclass.
 * @timeline: a pointer to a #GESTimeline
 *
 * Save data from timeline into formatter. Upon success, The data field of the
 * formatter will point to a newly-allocated block of data, and the length
 * field of the formatter will contain the length of the block. This method is
 * only implemented in subclasses.
 *
 * Returns: TRUE if the timeline data was successfully saved for FALSE if
 * an error occured during saving.
 */

gboolean
ges_formatter_save (GESFormatter * formatter, GESTimeline * timeline)
{
  GESFormatterClass *klass;

  klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->save)
    return klass->save (formatter, timeline);
  GST_ERROR ("not implemented!");
  return FALSE;
}

/**
 * ges_formatter_load_from_uri:
 * @formatter: a pointer to a #GESFormatter instance or subclass
 * @timeline: a pointer to a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * 
 * Load data from the given URI into timeline. The default implementation
 * loads the entire contents of the uri with g_file_get_contents, then calls
 * ges_formatter_load(). It works only on valid URIs pointing to local files.
 *
 * Subclasses should override the class method load_from_uri if they want to
 * handle other types of URIs. They should also override the class method
 * can_load_uri() to indicate that they can handle other types of URI.
 *
 * Returns: TRUE if the timeline data was successfully loaded from the URI or
 * FALSE if an error occured during loading.
 */

gboolean
ges_formatter_load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    gchar * uri)
{
  gchar *location;
  GError *e = NULL;
  gboolean ret = TRUE;


  if (formatter->data) {
    GST_ERROR ("formatter already has data! please set data to NULL");
  }

  if (!(location = gst_uri_get_location (uri))) {
    return FALSE;
  }

  if (g_file_get_contents (location, &formatter->data, &formatter->length, &e)) {
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
 * @formatter: a pointer to a #GESFormatter instance or subclass
 * @timeline: a pointer to a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 *
 * Save data from timeline to the given URI. The default implementation first
 * calls ges_formatter_save () and then writes the entire contents of the data
 * field to a local file using g_file_set_contents. It works only for local
 * files.
 *
 * Subclasses should override the class method save_to_uri if they want to
 * handle other types of URIs. They should also override the class method
 * can_save_uri to return true for custom URIs.
 *
 * Returns: TRUE if the timeline data was successfully saved to the URI or
 * FALSE if an error occured during saving.
 */

gboolean
ges_formatter_save_to_uri (GESFormatter * formatter, GESTimeline * timeline,
    gchar * uri)
{
  gchar *location;
  GError *e = NULL;
  gboolean ret = TRUE;


  if (!(location = g_filename_from_uri (uri, NULL, NULL))) {
    return FALSE;
  }

  if (!ges_formatter_save (formatter, timeline)) {
    GST_ERROR ("couldn't serialize formatter");
  } else {
    if (!g_file_set_contents (location, formatter->data, formatter->length, &e)) {
      GST_ERROR ("couldn't write file '%s': %s", location, e->message);
      ret = FALSE;
    }
  }

  if (e)
    g_error_free (e);
  g_free (location);

  return ret;
}
