#ifndef GST_GCONF_H
#define GST_GCONF_H

/*
 * this library handles interaction with GConf
 */

#include <gst/gst.h>
#include <gconf/gconf-client.h>

gchar *		gst_gconf_get_string 		(const gchar *key);
void		gst_gconf_set_string 		(const gchar *key, 
                                                 const gchar *value);

GstElement *	gst_gconf_render_bin_from_key		(const gchar *key);
GstElement *	gst_gconf_render_bin_from_description	(const gchar *description);

/*
guint		gst_gconf_notify_add		(const gchar *key,
    						 GConfClientNotifyFunc func,
						 gpointer user_data);
*/
#endif /* GST_GCONF_H */
