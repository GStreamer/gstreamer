/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsturi.c: register URI handlers
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsturi.h"
#include "gstinfo.h"
#include "gstregistrypool.h"
#include "gstmarshal.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_uri_handler_debug);
#define GST_CAT_DEFAULT gst_uri_handler_debug

static void	gst_uri_handler_base_init	(gpointer g_class);

GType
gst_uri_handler_get_type (void)
{
  static GType urihandler_type = 0;

  if (!urihandler_type) {
    static const GTypeInfo urihandler_info = {
      sizeof (GstURIHandlerInterface),
      gst_uri_handler_base_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
      NULL
    };
    urihandler_type = g_type_register_static (G_TYPE_INTERFACE,
	    "GstURIHandler", &urihandler_info, 0);

    GST_DEBUG_CATEGORY_INIT (gst_uri_handler_debug, "GST_URI", GST_DEBUG_BOLD, "handling of URIs");
  }
  return urihandler_type;
}
static void
gst_uri_handler_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    g_signal_new ("new-uri", GST_TYPE_URI_HANDLER, G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET (GstURIHandlerInterface, new_uri), NULL, NULL,
	    gst_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);
    initialized = TRUE;
  }
}

static void
gst_uri_protocol_check_internal (const gchar *uri, gchar **endptr)
{
  gchar *check = (gchar *) uri;
  
  g_assert (uri != NULL);
  g_assert (endptr != NULL);

  if (g_ascii_isalpha (*check)) {
    check++;
    while (g_ascii_isalnum (*check)) check++;
  }

  *endptr = check;
}
/**
 * gst_uri_protocol_is_valid:
 * @protocol: string to check
 *
 * Tests if the given string is a valid protocol identifier. Protocols
 * must consist of alphanumeric characters and not start with a number.
 *
 * Returns: TRUE if the string is a valid protocol identifier
 */
gboolean
gst_uri_protocol_is_valid (const gchar *protocol)
{
  gchar *endptr;
  
  g_return_val_if_fail (protocol != NULL, FALSE);
  
  gst_uri_protocol_check_internal (protocol, &endptr);

  return *endptr == '\0' && endptr != protocol;
}
/**
 * gst_uri_is_valid:
 * @protocol: string to check
 *
 * Tests if the given string is a valid URI identifier. URIs start with a valid
 * protocol followed by "://" and a string identifying the location.
 *
 * Returns: TRUE if the string is a valid URI
 */
gboolean
gst_uri_is_valid (const gchar *uri)
{
  gchar *endptr;
  
  g_return_val_if_fail (uri != NULL, FALSE);
  
  gst_uri_protocol_check_internal (uri, &endptr);

  return (*endptr == ':' &&
	  *(endptr + 1) == '/' &&
	  *(endptr + 2) == '/');
}
/**
 * gst_uri_get_protocol:
 * @uri: URI to get protocol from
 *
 * Extracts the protocol out of a given valid URI. The returned string must be
 * freed using g_free().
 *
 * Returns: The protocol for this URI.
 */
gchar *
gst_uri_get_protocol (const gchar *uri)
{
  gchar *colon;
  
  g_return_val_if_fail (uri != NULL, NULL);
  g_return_val_if_fail (gst_uri_is_valid (uri), NULL);

  colon = strstr (uri, "://");

  return g_strndup (uri, colon - uri);
}
/**
 * gst_uri_get_location:
 * @uri: URI to get the location from
 *
 * Extracts the location out of a given valid URI. So the protocol and "://"
 * are stripped from the URI. The returned string must be freed using 
 * g_free().
 *
 * Returns: The location for this URI.
 */
gchar *
gst_uri_get_location (const gchar *uri)
{
  gchar *colon;
  
  g_return_val_if_fail (uri != NULL, NULL);
  g_return_val_if_fail (gst_uri_is_valid (uri), NULL);

  colon = strstr (uri, "://");

  return g_strdup (colon + 3);
}
/**
 * gst_uri_construct:
 * @protocol: protocol for URI
 * @location: location for URI
 *
 * Constructs a URI for a given valid protocol and location.
 *
 * Returns: a new string for this URI
 */
gchar *
gst_uri_construct (const gchar *protocol, const gchar *location)
{
  g_return_val_if_fail (gst_uri_protocol_is_valid (protocol), NULL);
  g_return_val_if_fail (location != NULL, NULL);

  return g_strdup_printf ("%s://%s", protocol, location);
}
typedef struct{
  GstURIType	type;
  gchar *	protocol;
} SearchEntry;
static gboolean
search_by_entry (GstPluginFeature *feature, gpointer search_entry)
{
  gchar **protocols;
  GstElementFactory *factory;
  SearchEntry *entry = (SearchEntry *) search_entry;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;
  factory = GST_ELEMENT_FACTORY (feature);

  if (gst_element_factory_get_uri_type (factory) != entry->type)
    return FALSE;
  
  protocols = gst_element_factory_get_uri_protocols (factory);
  /* must be set when uri type is valid */
  g_assert (protocols);
  while (*protocols != NULL) {
    if (strcmp (*protocols, entry->protocol) == 0)
      return TRUE;
    protocols++;
  }
  return FALSE;
}
static gint
sort_by_rank (gconstpointer a, gconstpointer b)
{
  GstPluginFeature *first = GST_PLUGIN_FEATURE (a);
  GstPluginFeature *second = GST_PLUGIN_FEATURE (b);

  return gst_plugin_feature_get_rank (second) - gst_plugin_feature_get_rank (first);
}
/**
 * gst_element_make_from_uri:
 * @type: wether to create a source or a sink
 * @uri: URI to create element for
 * @elementname: optional name of created element
 *
 * Creates an element for handling the given URI. 
 * 
 * Returns: a new element or NULL if none could be created
 */
GstElement *
gst_element_make_from_uri (const GstURIType type, const gchar *uri, const gchar *elementname)
{
  GList *possibilities, *walk;
  SearchEntry entry;
  GstElement *ret = NULL;

  g_return_val_if_fail (GST_URI_TYPE_IS_VALID (type), NULL);
  g_return_val_if_fail (gst_uri_is_valid (uri), NULL);

  entry.type = type;
  entry.protocol = gst_uri_get_protocol (uri);
  possibilities = gst_registry_pool_feature_filter (search_by_entry, FALSE, &entry);
  g_free (entry.protocol);

  if (!possibilities) {
    GST_DEBUG ("No %s for URI '%s'", type == GST_URI_SINK ? "sink" : "source", uri);
    return NULL;
  }
  
  possibilities = g_list_sort (possibilities, sort_by_rank);
  walk = possibilities;
  while (walk) {
    if ((ret = gst_element_factory_create (GST_ELEMENT_FACTORY (walk->data), 
		  elementname)) != NULL) {
      GstURIHandler *handler = GST_URI_HANDLER (ret);
      if (gst_uri_handler_set_uri (handler, uri))
	break;
      g_object_unref (ret);
      ret = NULL;
    }
  }
  g_list_free (possibilities);

  GST_LOG_OBJECT (ret, "created %s for URL '%s'", type == GST_URI_SINK ? "sink" : "source", uri);
  return ret;
}
/**
 * gst_uri_handler_get_uri_type:
 * @handler: Handler to query type of
 *
 * Gets the type of a URI handler
 *
 * Returns: the type of the URI handler
 */
guint
gst_uri_handler_get_uri_type (GstURIHandler *handler)
{
  GstURIHandlerInterface *iface;
  guint ret;
  
  g_return_val_if_fail (GST_IS_URI_HANDLER (handler), GST_URI_UNKNOWN);

  iface = GST_URI_HANDLER_GET_INTERFACE (handler);
  g_return_val_if_fail (iface != NULL, GST_URI_UNKNOWN);
  g_return_val_if_fail (iface->get_type != NULL, GST_URI_UNKNOWN);
  ret = iface->get_type ();
  g_return_val_if_fail (GST_URI_TYPE_IS_VALID (ret), GST_URI_UNKNOWN);

  return ret;
}
/**
 * gst_uri_handler_get_protocols:
 * @handler: Handler to get protocols for
 *
 * Gets the list of supported protocols for this handler. This list may not be
 * modified.
 *
 * Returns: the supported protocols
 */
gchar **
gst_uri_handler_get_protocols (GstURIHandler *handler)
{
  GstURIHandlerInterface *iface;
  gchar **ret;
  
  g_return_val_if_fail (GST_IS_URI_HANDLER (handler), NULL);

  iface = GST_URI_HANDLER_GET_INTERFACE (handler);
  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_protocols != NULL, NULL);
  ret = iface->get_protocols ();
  g_return_val_if_fail (ret != NULL, NULL);

  return ret;
}
/**
 * gst_uri_handler_get_uri:
 * @handler: handler to query URI of
 *
 * Gets the currently handled URI of the handler or NULL, if none is set.
 *
 * Returns: the URI
 */
G_CONST_RETURN gchar *
gst_uri_handler_get_uri (GstURIHandler *handler)
{
  GstURIHandlerInterface *iface;
  const gchar *ret;
  
  g_return_val_if_fail (GST_IS_URI_HANDLER (handler), NULL);

  iface = GST_URI_HANDLER_GET_INTERFACE (handler);
  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_uri != NULL, NULL);
  ret = iface->get_uri (handler);
  if (ret != NULL)
    g_return_val_if_fail (gst_uri_is_valid (ret), NULL);
    
  return ret;
}
/**
 * gst_uri_handler_set_uri:
 * @handler: handler to set URI of
 * @uri: URI to set
 *
 * Tries to set the URI of the given handler and returns TRUE if it succeeded.
 *
 * Returns: TRUE, if the URI was set successfully
 */
gboolean
gst_uri_handler_set_uri (GstURIHandler *handler, const gchar *uri)
{
  GstURIHandlerInterface *iface;
  
  g_return_val_if_fail (GST_IS_URI_HANDLER (handler), FALSE);
  g_return_val_if_fail (gst_uri_is_valid (uri), FALSE);

  iface = GST_URI_HANDLER_GET_INTERFACE (handler);
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->set_uri != NULL, FALSE);
  return iface->set_uri (handler, uri);
}
/**
 * gst_uri_handler_new_uri:
 * @handler: handler with a new URI
 * @uri: new URI or NULL if it was unset
 *
 * Emits the new-uri event for a given handler, when that handler has a new URI.
 * This function should only be called by URI handlers themselves.
 */
void
gst_uri_handler_new_uri (GstURIHandler *handler, const gchar *uri)
{
  g_return_if_fail (GST_IS_URI_HANDLER (handler));

  g_signal_emit_by_name (handler, "new-uri", uri);
}
