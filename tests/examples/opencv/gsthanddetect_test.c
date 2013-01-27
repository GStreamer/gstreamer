/*
 *============================================================================
 *Name        : gsthanddetect_app.c
 *Author      : Andol Li, andol@andol.info
 *Version     : 0.1
 *Copyright   : @2012, gstreamer
 *Description : gsteramer handdetect plugin demo application in C, part work of GSoc 2012 project
 *============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include <glib.h>
#include <glib-object.h>

#define PROFILE_FIST "/usr/local/share/opencv/haarcascades/fist.xml"
#define PROFILE_PALM "/usr/local/share/opencv/haarcascades/palm.xml"

GstElement *playbin,
			*pipeline,
			*v4l2src, *videoscale, *ffmpegcolorspace_in, *handdetect, *ffmpegcolorspace_out, *xvimagesink;

static GstBusSyncHandler
bus_sync_handler(GstBus *bus, GstMessage *message, GstPipeline *pipeline)
{
	/* select msg */
	if(GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT ||
			!gst_structure_has_name(message->structure, "hand-gesture") )
		return GST_BUS_PASS;

	/* parse msg structure */
	const GstStructure *structure = message->structure;

		/* if PALM gesture detected */
		if (structure &&
				strcmp (gst_structure_get_name (structure), "hand-gesture") == 0 &&
				strcmp (gst_structure_get_string (structure, "gesture"), "palm") == 0) {
			/* media operation - closed palm to stop media play*/
			gst_element_set_state (playbin, GST_STATE_PAUSED);
		}

		/* if FIST gesture detected */
		if (structure &&
				strcmp (gst_structure_get_name (structure), "hand-gesture") == 0 &&
				strcmp (gst_structure_get_string (structure, "gesture"), "fist") == 0){
			/* print message type and structure name */
			g_print("%s{{%s}}\n",	gst_message_type_get_name(message->type), gst_structure_get_name(structure));
			/* print msg structure names&values */
			int i;
			for(i = 0; i < gst_structure_n_fields(structure); i++){
				const gchar *name = gst_structure_nth_field_name(structure, i);
				GType type = gst_structure_get_field_type(structure, name);
				const GValue *value = gst_structure_get_value(structure, name);
				type == G_TYPE_STRING ?
						g_print("-%s[%s]{%s}\n", name, g_type_name(type), g_value_get_string(value)) :
						g_print("-%s[%s]{%d}\n", name, g_type_name(type), g_value_get_uint(value));
			}
			g_print("\n");

			/* get X,Y positions in frame */
			const GValue *x_value = gst_structure_get_value(structure, "x");
			gint x = g_value_get_uint(x_value);
			const GValue *y_value = gst_structure_get_value(structure, "y");
			gint y = g_value_get_uint(y_value);

			/* set object volumes [0-10] based on Y */
			g_object_set(G_OBJECT(playbin), "volume", (gdouble)(10 - y/24 ), NULL);

			/* seek playback positions */
			gint64 position, length;
			GstFormat format = GST_FORMAT_TIME;
			gst_element_query_duration(playbin, &format, &length);
			/* Width = 320 is specified in caps */
			position = (gint64) length * x / 320;
			gst_element_set_state(playbin, GST_STATE_PAUSED);
			gst_element_seek(GST_ELEMENT(playbin),
					1.0,
					format,
					GST_SEEK_FLAG_FLUSH,
					GST_SEEK_TYPE_SET,
					position,
					GST_SEEK_TYPE_NONE,
					GST_CLOCK_TIME_NONE );
			gst_element_set_state(GST_ELEMENT(playbin), GST_STATE_PLAYING);
		}

	gst_message_unref(message);
	return GST_BUS_DROP;
}

int main(gint argc, gchar **argv) {
	static GMainLoop *loop;
	loop = g_main_loop_new(NULL, FALSE);
	/* video source */
	gchar *video_device = "/dev/video0";
	gchar *video_file = "file:///home/javauser/workspace/gitfiles/gsthanddetect_app/video.avi";
	/* bus */
	GstBus *bus;
	/* caps */
	GstCaps *caps;

	/* init gst */
	gst_init(&argc, &argv);

	/* init elements */
	playbin = gst_element_factory_make("playbin2", "app_playbin");
	pipeline = gst_pipeline_new("app_pipeline");
	v4l2src = gst_element_factory_make("v4l2src", "app_v4l2src");
	videoscale = gst_element_factory_make("videoscale", "app_videoscale");
	ffmpegcolorspace_in = gst_element_factory_make("ffmpegcolorspace", "app_ffmpegcolorspace_in");
	handdetect = gst_element_factory_make("handdetect", "app_handdetect");
	ffmpegcolorspace_out = gst_element_factory_make("ffmpegcolorspace", "app_ffmpegcolorspace_out");
	xvimagesink = gst_element_factory_make("xvimagesink", "app_xvimagesink");

	/* check init results */
	if(!playbin || !pipeline || !v4l2src || !videoscale || !ffmpegcolorspace_in
			|| !handdetect || !ffmpegcolorspace_out || !xvimagesink)
		g_error("ERROR: element init failed.\n");

	/* set values */
	g_object_set (G_OBJECT(playbin), "uri", video_file, NULL);
	g_object_set (G_OBJECT(v4l2src), "device", video_device, NULL);
	g_object_set (G_OBJECT (handdetect), "profile_fist", PROFILE_FIST, NULL);
	g_object_set (G_OBJECT (handdetect), "profile_palm", PROFILE_PALM, NULL);

	/* set caps */
	caps = gst_caps_from_string("video/x-raw-rgb, width=320, height=240, framerate=(fraction)30/1");

	/* set bus */
	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	gst_bus_set_sync_handler(bus, (GstBusSyncHandler) bus_sync_handler, pipeline);
	gst_object_unref(bus);

	/* add elements to pipeline */
	gst_bin_add_many(GST_BIN(pipeline),
			v4l2src,
			videoscale,
			ffmpegcolorspace_in,
			handdetect,
			ffmpegcolorspace_out,
			xvimagesink,
			NULL);

	/* negotiate caps */
	if(!gst_element_link_filtered( v4l2src, videoscale, caps)){
			g_printerr("ERROR:v4l2src -> videoscale caps\n");
			return 0; }
	gst_caps_unref(caps);

	/* link elements */
	gst_element_link_many(
			videoscale,
			ffmpegcolorspace_in,
			handdetect,
			ffmpegcolorspace_out,
			xvimagesink,
			NULL);

	/* change states */
	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	/* start main loop */
	g_main_loop_run(loop);

	/* clean all */
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(pipeline));
	gst_element_set_state(playbin, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(playbin));

	return 0;
}
