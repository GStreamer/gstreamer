/*
 * this library handles interaction with GConf
 */

#include "gconf.h"

#define GST_GCONF_DIR "/system/gstreamer"

static GConfClient *_gst_gconf_client = NULL; /* GConf connection */


/* internal functions */

GConfClient *
gst_gconf_get_client (void)
{
  if (!_gst_gconf_client)
    _gst_gconf_client = gconf_client_get_default ();

  return _gst_gconf_client;
}
/* go through a bin, finding the one pad that is unconnected in the given
 *  * direction, and return that pad */
GstPad *
gst_bin_find_unconnected_pad (GstBin *bin, GstPadDirection direction)
{
  GstPad *pad = NULL;
  GList *elements = NULL;
  GList *pads = NULL;
  GstElement *element = NULL;

  g_print ("DEBUG: find_unconnected start\n");
  elements = (GList *) gst_bin_get_list (bin);
  /* traverse all elements looking for unconnected pads */
  while (elements && pad == NULL)
  {
    element = GST_ELEMENT (elements->data);
    g_print ("DEBUG: looking in element %s\n", gst_element_get_name (element));
    pads = gst_element_get_pad_list (element);
    while (pads)
    {
      /* check if the direction matches */
      if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == direction)
      {
        if (GST_PAD_PEER (GST_PAD (pads->data)) == NULL)
        {
          /* found it ! */
	  g_print ("DEBUG: found an unconnected pad !\n");
	  pad = GST_PAD (pads->data);
	}
      }
      if (pad) break; /* found one already */
      pads = g_list_next (pads);
    }
    elements = g_list_next (elements);
  }
  g_print ("DEBUG: find_unconnected stop\n");
  return pad;
}

/* external functions */

gchar *
gst_gconf_get_string (const gchar *key)
{
  GError *error = NULL;
  gchar *value = NULL;
  gchar *full_key = g_strdup_printf ("%s/%s", GST_GCONF_DIR, key); 

  g_print ("DEBUG: full key: %s\n", full_key);

  value = gconf_client_get_string (gst_gconf_get_client (), full_key, &error);
  g_free (full_key);

  if (value) 
    return value;
  else
    return NULL;
  // this is a good idea: return g_strdup (default_val);
}

void
gst_gconf_set_string (const gchar *key, const gchar *value)
{
  gconf_client_set_string (gst_gconf_get_client (), key, value, NULL);
}

/* this function renders the given description to a bin,
 * and ghosts at most one unconnected src pad and one unconnected sink pad */
GstElement *
gst_gconf_render_bin_from_description (const gchar *description)
{
  GstElement *bin = NULL;
  GstPad *pad = NULL;
  GError *error = NULL;
  gchar *desc = NULL;

  /* parse the pipeline to a bin */
  desc = g_strdup_printf ("bin.( %s )", description);
  bin = GST_ELEMENT (gst_parse_launch (desc, &error));
  g_free (desc);
  if (error)
  {
    g_print ("DEBUG: gstgconf: error parsing pipeline %s\n%s\n",
	     description, error->message);
    g_error_free (error);
    return NULL;
  }

  /* find pads and ghost them if necessary */
  if ((pad = gst_bin_find_unconnected_pad (GST_BIN (bin), GST_PAD_SRC)))
    gst_element_add_ghost_pad (bin, pad, "src");
  if ((pad = gst_bin_find_unconnected_pad (GST_BIN (bin), GST_PAD_SINK)))
    gst_element_add_ghost_pad (bin, pad, "sink");
  return bin;
}

/* this function reads the gconf key, parses the pipeline bit to a bin,
 * and ghosts at most one unconnected src pad and one unconnected sink pad */
GstElement *
gst_gconf_render_bin_from_key (const gchar *key)
{
  GstElement *bin;
  gchar *description;
  gchar *value = NULL;
  
  value = gst_gconf_get_string (key);
  description = g_strdup_printf ("bin.( %s )", value);
  bin = gst_gconf_render_bin_from_description (description);
  g_free (description);
  return bin;
}

/*
guint		gst_gconf_notify_add		(const gchar *key,
    						 GConfClientNotifyFunc func,
						 gpointer user_data);
*/

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
    gst_plugin_set_longname (plugin, 
	                     "Convenience routines for GConf interaction");
      return TRUE;
}

GstPluginDesc plugin_desc = {
    GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
        "gstgconf",
	  plugin_init
};

