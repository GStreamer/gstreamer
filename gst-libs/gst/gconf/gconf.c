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

gboolean
gst_gconf_render_bin (const gchar *key, GstElement **element)
{
  GstElement *bin;
  gchar *pipeline;
  GError *error = NULL;
  gchar *value = NULL;
  
  value = gst_gconf_get_string (key);

  pipeline = g_strdup_printf ("bin.( %s )", value);
  bin = GST_ELEMENT (gst_parse_launch (pipeline, &error));
  if (error)
  {
    g_print ("DEBUG: gstgconf: error parsing pipeline %s\n%s\n",
	     pipeline, error->message);
    g_error_free (error);
    return FALSE;
  }
  *element = bin;
  return TRUE;
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

