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

gboolean	gst_gconf_render_bin		(const gchar *key,
    						 GstElement **element);

/*
guint		gst_gconf_notify_add		(const gchar *key,
    						 GConfClientNotifyFunc func,
						 gpointer user_data);
*/
#endif /* GST_GCONF_H */
