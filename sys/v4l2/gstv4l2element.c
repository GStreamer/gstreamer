/* G-Streamer generic V4L2 element
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include "v4l2_calls.h"


static GstElementDetails gst_v4l2element_details = {
	"Generic video4linux2 Element",
	"None/Video",
	"Generic plugin for handling common video4linux2 calls",
	VERSION,
	"Ronald Bultje <rbultje@ronald.bitfreak.net>",
	"(C) 2002",
};

/* V4l2Element signals and args */
enum {
	/* FILL ME */
	LAST_SIGNAL
};

enum {
	ARG_0,
	ARG_CHANNEL,
	ARG_CHANNEL_NAMES,
	ARG_OUTPUT,
	ARG_OUTPUT_NAMES,
	ARG_NORM,
	ARG_NORM_NAMES,
	ARG_HAS_TUNER,
	ARG_FREQUENCY,
	ARG_SIGNAL_STRENGTH,
	ARG_HAS_AUDIO,
	ARG_ATTRIBUTE,
	ARG_ATTRIBUTE_SETS,
	ARG_DEVICE,
	ARG_DEVICE_NAME,
	ARG_DEVICE_HAS_CAPTURE,
	ARG_DEVICE_HAS_OVERLAY,
	ARG_DEVICE_HAS_CODEC,
	ARG_DISPLAY,
	ARG_VIDEOWINDOW,
	ARG_CLIPPING,
	ARG_DO_OVERLAY,
};


static void			gst_v4l2element_class_init	(GstV4l2ElementClass *klass);
static void			gst_v4l2element_init		(GstV4l2Element      *v4lelement);
static void			gst_v4l2element_set_property	(GObject             *object,
								 guint               prop_id,
								 const GValue        *value,
								 GParamSpec          *pspec);
static void			gst_v4l2element_get_property	(GObject             *object,
								 guint               prop_id,
								 GValue              *value,
								 GParamSpec          *pspec);
static GstElementStateReturn	gst_v4l2element_change_state	(GstElement          *element);


static GstElementClass *parent_class = NULL;
/*static guint gst_v4l2element_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_v4l2element_get_type (void)
{
	static GType v4l2element_type = 0;

	if (!v4l2element_type) {
		static const GTypeInfo v4l2element_info = {
			sizeof(GstV4l2ElementClass),
			NULL,
			NULL,
			(GClassInitFunc) gst_v4l2element_class_init,
			NULL,
			NULL,
			sizeof(GstV4l2Element),
			0,
			(GInstanceInitFunc) gst_v4l2element_init,
			NULL
		};
		v4l2element_type = g_type_register_static(GST_TYPE_ELEMENT,
					"GstV4l2Element", &v4l2element_info, 0);
	}
	return v4l2element_type;
}



static void
gst_v4l2element_class_init (GstV4l2ElementClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNEL,
		g_param_spec_int("channel","channel","channel",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNEL_NAMES,
		g_param_spec_pointer("channel_names","channel_names","channel_names",
		G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OUTPUT,
		g_param_spec_int("output","output","output",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OUTPUT_NAMES,
		g_param_spec_pointer("output_names","output_names","output_names",
		G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NORM,
		g_param_spec_int("norm","norm","norm",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NORM_NAMES,
		g_param_spec_pointer("norm_names","norm_names","norm_names",
		G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HAS_TUNER,
		g_param_spec_boolean("has_tuner","has_tuner","has_tuner",
		0,G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQUENCY,
		g_param_spec_ulong("frequency","frequency","frequency",
		0,G_MAXULONG,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SIGNAL_STRENGTH,
		g_param_spec_ulong("signal_strength","signal_strength","signal_strength",
		0,G_MAXULONG,0,G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HAS_AUDIO,
		g_param_spec_boolean("has_audio","has_audio","has_audio",
		0,G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ATTRIBUTE,
		g_param_spec_pointer("attribute","attribute","attribute",
		G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ATTRIBUTE_SETS,
		g_param_spec_pointer("attribute_sets","attribute_sets","attribute_sets",
		G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
		g_param_spec_string("device","device","device",
		NULL, G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_NAME,
		g_param_spec_string("device_name","device_name","device_name",
		NULL, G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_HAS_CAPTURE,
		g_param_spec_boolean("can_capture","can_capture","can_capture",
		0,G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_HAS_OVERLAY,
		g_param_spec_boolean("has_overlay","has_overlay","has_overlay",
		0,G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_HAS_CODEC,
		g_param_spec_boolean("has_compression","has_compression","has_compression",
		0,G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DISPLAY,
		g_param_spec_string("display","display","display",
		NULL, G_PARAM_WRITABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DO_OVERLAY,
		g_param_spec_boolean("do_overlay","do_overlay","do_overlay",
		0,G_PARAM_WRITABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VIDEOWINDOW,
		g_param_spec_pointer("videowindow","videowindow","videowindow",
		G_PARAM_WRITABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CLIPPING,
		g_param_spec_pointer("videowindowclip","videowindowclip","videowindowclip",
		G_PARAM_WRITABLE));

	gobject_class->set_property = gst_v4l2element_set_property;
	gobject_class->get_property = gst_v4l2element_get_property;

	gstelement_class->change_state = gst_v4l2element_change_state;
}


static void
gst_v4l2element_init (GstV4l2Element *v4l2element)
{
	/* some default values */
	v4l2element->video_fd = -1;
	v4l2element->buffer = NULL;
	v4l2element->device = NULL;

	v4l2element->norm = -1;
	v4l2element->channel = -1;
	v4l2element->output = -1;
	v4l2element->frequency = 0;

	v4l2element->controls = NULL;
	v4l2element->formats = NULL;
	v4l2element->outputs = NULL;
	v4l2element->inputs = NULL;
	v4l2element->norms = NULL;
}


static void
gst_v4l2element_set_property (GObject      *object,
                              guint        prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	GstV4l2Element *v4l2element;

	/* it's not null if we got it, but it might not be ours */
	g_return_if_fail(GST_IS_V4L2ELEMENT(object));
	v4l2element = GST_V4L2ELEMENT(object);

	switch (prop_id) {
		case ARG_CHANNEL:
			v4l2element->channel = g_value_get_int(value);
			if (GST_V4L2_IS_OPEN(v4l2element) && !GST_V4L2_IS_ACTIVE(v4l2element)) {
				if (!gst_v4l2_set_input(v4l2element, g_value_get_int(value)))
					return;
			}
			break;
		case ARG_OUTPUT:
			v4l2element->output = g_value_get_int(value);
			if (GST_V4L2_IS_OPEN(v4l2element) && !GST_V4L2_IS_ACTIVE(v4l2element)) {
				if (!gst_v4l2_set_output(v4l2element, g_value_get_int(value)))
					return;
			}
			break;
		case ARG_NORM:
			v4l2element->norm = g_value_get_int(value);
			if (GST_V4L2_IS_OPEN(v4l2element) && !GST_V4L2_IS_ACTIVE(v4l2element)) {
				if (!gst_v4l2_set_norm(v4l2element, g_value_get_int(value)))
					return;
			}
			break;
		case ARG_FREQUENCY:
			v4l2element->frequency = g_value_get_ulong(value);
			if (GST_V4L2_IS_OPEN(v4l2element) && !GST_V4L2_IS_ACTIVE(v4l2element)) {
				if (!gst_v4l2_set_frequency(v4l2element, g_value_get_ulong(value)))
					return;
			}
			break;
		case ARG_ATTRIBUTE:
			if (GST_V4L2_IS_OPEN(v4l2element)) {
				gst_v4l2_set_attribute(v4l2element,
					((GstV4l2Attribute*)g_value_get_pointer(value))->index,
					((GstV4l2Attribute*)g_value_get_pointer(value))->value);
			}
			break;
		case ARG_DEVICE:
			if (!GST_V4L2_IS_OPEN(v4l2element)) {
				if (v4l2element->device)
					g_free(v4l2element->device);
				v4l2element->device = g_strdup(g_value_get_string(value));
			}
			break;
		case ARG_DISPLAY:
			if (!gst_v4l2_set_display(v4l2element, g_value_get_string(value)))
				return;
			break;
		case ARG_VIDEOWINDOW:
			if (GST_V4L2_IS_OPEN(v4l2element)) {
				gst_v4l2_set_window(v4l2element,
					((GstV4l2Rect*)g_value_get_pointer(value))->x,
					((GstV4l2Rect*)g_value_get_pointer(value))->y,
					((GstV4l2Rect*)g_value_get_pointer(value))->w,
					((GstV4l2Rect*)g_value_get_pointer(value))->h);
			}
			break;
		case ARG_CLIPPING:
			if (GST_V4L2_IS_OPEN(v4l2element)) {
				gint i;
				struct v4l2_clip *clips;
				GList *list = (GList*)g_value_get_pointer(value);
				clips = g_malloc(sizeof(struct v4l2_clip) * g_list_length(list));
				for (i=0;i<g_list_length(list);i++)
				{
					clips[i].x = ((GstV4l2Rect*)g_list_nth_data(list, i))->x;
					clips[i].y = ((GstV4l2Rect*)g_list_nth_data(list, i))->y;
					clips[i].width = ((GstV4l2Rect*)g_list_nth_data(list, i))->w;
					clips[i].height = ((GstV4l2Rect*)g_list_nth_data(list, i))->h;
				}
				gst_v4l2_set_clips(v4l2element, clips, g_list_length(list));
        			g_free(clips);
			}
			break;
		case ARG_DO_OVERLAY:
			if (GST_V4L2_IS_OPEN(v4l2element)) {
				if (!gst_v4l2_enable_overlay(v4l2element, g_value_get_boolean(value)))
					return;
			}
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


static void
gst_v4l2element_get_property (GObject    *object,
                              guint      prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	GstV4l2Element *v4l2element;
	gint temp_i = 0;
	gulong temp_ul = 0;
	GList *list = NULL;

	/* it's not null if we got it, but it might not be ours */
	g_return_if_fail(GST_IS_V4L2ELEMENT(object));
	v4l2element = GST_V4L2ELEMENT(object);

	switch (prop_id) {
		case ARG_CHANNEL:
			if (GST_V4L2_IS_OPEN(v4l2element))
				gst_v4l2_get_input(v4l2element, &temp_i);
			g_value_set_int(value, temp_i);
			break;
		case ARG_CHANNEL_NAMES:
			if (GST_V4L2_IS_OPEN(v4l2element))
				list = gst_v4l2_get_input_names(v4l2element);
			g_value_set_pointer(value, list);
			break;
		case ARG_OUTPUT:
			if (GST_V4L2_IS_OPEN(v4l2element))
				gst_v4l2_get_output(v4l2element, &temp_i);
			g_value_set_int(value, temp_i);
			break;
		case ARG_OUTPUT_NAMES:
			if (GST_V4L2_IS_OPEN(v4l2element))
				list = gst_v4l2_get_output_names(v4l2element);
			g_value_set_pointer(value, list);
			break;
		case ARG_NORM:
			if (GST_V4L2_IS_OPEN(v4l2element))
				gst_v4l2_get_norm(v4l2element, &temp_i);
			g_value_set_int(value, temp_i);
			break;
		case ARG_NORM_NAMES:
			if (GST_V4L2_IS_OPEN(v4l2element))
				list = gst_v4l2_get_norm_names(v4l2element);
			g_value_set_pointer(value, list);
			break;
		case ARG_HAS_TUNER:
			if (GST_V4L2_IS_OPEN(v4l2element))
				temp_i = gst_v4l2_has_tuner(v4l2element);
			g_value_set_boolean(value, temp_i>0?TRUE:FALSE);
			break;
		case ARG_FREQUENCY:
			if (GST_V4L2_IS_OPEN(v4l2element))
				gst_v4l2_get_frequency(v4l2element, &temp_ul);
			g_value_set_ulong(value, temp_ul);
			break;
		case ARG_SIGNAL_STRENGTH:
			if (GST_V4L2_IS_OPEN(v4l2element))
				gst_v4l2_signal_strength(v4l2element, &temp_ul);
			g_value_set_ulong(value, temp_ul);
			break;
		case ARG_HAS_AUDIO:
			if (GST_V4L2_IS_OPEN(v4l2element))
				temp_i = gst_v4l2_has_audio(v4l2element);
			g_value_set_boolean(value, temp_i>0?TRUE:FALSE);
			break;
		case ARG_ATTRIBUTE:
			if (GST_V4L2_IS_OPEN(v4l2element))
				gst_v4l2_get_attribute(v4l2element,
					((GstV4l2Attribute*)g_value_get_pointer(value))->index, &temp_i);
			((GstV4l2Attribute*)g_value_get_pointer(value))->value = temp_i;
			break;
		case ARG_ATTRIBUTE_SETS:
			if (GST_V4L2_IS_OPEN(v4l2element))
				list = gst_v4l2_get_attributes(v4l2element);
			g_value_set_pointer(value, list);
			break;
		case ARG_DEVICE:
			g_value_set_string(value, g_strdup(v4l2element->device));
			break;
		case ARG_DEVICE_NAME:
			if (GST_V4L2_IS_OPEN(v4l2element))
				g_value_set_string(value, g_strdup(v4l2element->vcap.name));
			break;
		case ARG_DEVICE_HAS_CAPTURE:
			if (GST_V4L2_IS_OPEN(v4l2element) &&
			    (v4l2element->vcap.type == V4L2_TYPE_CODEC ||
			     v4l2element->vcap.type == V4L2_TYPE_CAPTURE) &&
			    v4l2element->vcap.flags & V4L2_FLAG_STREAMING)
				temp_i = 1;
			g_value_set_boolean(value, temp_i>0?TRUE:FALSE);
			break;
		case ARG_DEVICE_HAS_OVERLAY:
			if (GST_V4L2_IS_OPEN(v4l2element) &&
			    v4l2element->vcap.flags & V4L2_FLAG_PREVIEW)
				temp_i = 1;
			g_value_set_boolean(value, temp_i>0?TRUE:FALSE);
			break;
		case ARG_DEVICE_HAS_CODEC:
			if (GST_V4L2_IS_OPEN(v4l2element) &&
			    v4l2element->vcap.type == V4L2_TYPE_CODEC)
				temp_i = 1;
			g_value_set_boolean(value, temp_i>0?TRUE:FALSE);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstElementStateReturn
gst_v4l2element_change_state (GstElement *element)
{
	GstV4l2Element *v4l2element;

	g_return_val_if_fail(GST_IS_V4L2ELEMENT(element), GST_STATE_FAILURE);
  
	v4l2element = GST_V4L2ELEMENT(element);

	/* if going down into NULL state, close the device if it's open
	 * if going to READY, open the device (and set some options)
	 */
	switch (GST_STATE_TRANSITION(element)) {
		case GST_STATE_NULL_TO_READY:
			if (!gst_v4l2_open(v4l2element))
				return GST_STATE_FAILURE;

			/* now, sync options */
			if (v4l2element->norm >= 0)
				if (!gst_v4l2_set_norm(v4l2element, v4l2element->norm))
					return GST_STATE_FAILURE;
			if (v4l2element->channel >= 0)
				if (!gst_v4l2_set_input(v4l2element, v4l2element->channel))
					return GST_STATE_FAILURE;
			if (v4l2element->output >= 0)
				if (!gst_v4l2_set_output(v4l2element, v4l2element->output))
					return GST_STATE_FAILURE;
			if (v4l2element->frequency > 0)
				if (!gst_v4l2_set_frequency(v4l2element, v4l2element->frequency))
					return GST_STATE_FAILURE;
			break;
		case GST_STATE_READY_TO_NULL:
			if (!gst_v4l2_close(v4l2element))
				return GST_STATE_FAILURE;
			break;
	}

	if (GST_ELEMENT_CLASS(parent_class)->change_state)
		return GST_ELEMENT_CLASS(parent_class)->change_state(element);

	return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
	GstElementFactory *factory;

	/* create an elementfactory for the v4l2element */
	factory = gst_element_factory_new("v4l2element", GST_TYPE_V4L2ELEMENT,
				&gst_v4l2element_details);
	g_return_val_if_fail(factory != NULL, FALSE);
	gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

	return TRUE;
}


GstPluginDesc plugin_desc = {
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"v4l2element",
	plugin_init
};
