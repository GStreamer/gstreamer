#ifndef __GSTPLAY_PRIVATE_H__
#define __GSTPLAY_PRIVATE_H__

#include <gst/gst.h>

typedef struct _GstPlayPrivate GstPlayPrivate;

struct _GstPlayPrivate {
	GstElement *pipeline;
	GstElement *video_element, *audio_element;
	GstElement *video_show;
	GtkWidget  *video_widget;
	GstElement *src;
	GstElement *cache;
	GstElement *typefind;
	
	guchar *uri;
	gboolean muted;
	gboolean can_seek;
	
	GstElement *offset_element;
	GstElement *bit_rate_element;
	GstElement *media_time_element;
	GstElement *current_time_element;

	guint source_width;
	guint source_height;
};

#endif /* __GSTPLAY_PRIVATE_H__ */
