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

GstElement *	gst_gconf_get_default_video_sink (void);
GstElement *	gst_gconf_get_default_audio_sink (void);
GstElement *	gst_gconf_get_default_video_src (void);
GstElement *	gst_gconf_get_default_audio_src (void);
GstElement *	gst_gconf_get_default_visualisation_element (void);

#endif /* GST_GCONF_H */
