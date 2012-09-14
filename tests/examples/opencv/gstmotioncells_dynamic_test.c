/* GStreamer
 * Copyright (C) 2011 Robert Jobbagy <jobbagy.robert@gmail.com>
 *
 *
 *  gst_motioncells_dynamic_test(): a test tool what can to do dynamic change properties
 *
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <gst/gst.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include "gstmotioncells_dynamic_test.h"
#include "gst_element_print_properties.h"

const guint c2w = 21;           // column 2 width
const guint c3w = 19;           // column 3 width
const guint c4w = 23;           // column 4 width

void
setProperty (GstElement * mcells, char *property, char *prop_value, GType type,
    GValue * value)
{

  switch (type) {
    case G_TYPE_STRING:
    {
      g_object_set (G_OBJECT (mcells), property, prop_value, NULL);
      break;
    }
    case G_TYPE_BOOLEAN:
    {
      gboolean flag = (g_strcmp0 (prop_value, "true") == 0) ? TRUE : FALSE;
      g_object_set (G_OBJECT (mcells), property, flag, NULL);
      break;
    }
    case G_TYPE_ULONG:
    {
      unsigned long ulongval = strtoul (prop_value, NULL, 0);
      g_object_set (G_OBJECT (mcells), property, ulongval, NULL);
      break;
    }
    case G_TYPE_LONG:
    {
      long longval = atol (prop_value);
      g_object_set (G_OBJECT (mcells), property, longval, NULL);
      break;
    }
    case G_TYPE_UINT:
    {
      unsigned int uintval = atoi (prop_value);
      g_object_set (G_OBJECT (mcells), property, uintval, NULL);
      break;
    }
    case G_TYPE_INT:
    {
      int intval = atoi (prop_value);
      g_object_set (G_OBJECT (mcells), property, intval, NULL);
      break;
    }
    case G_TYPE_UINT64:
    {
      guint64 guint64val = atoi (prop_value);
      g_object_set (G_OBJECT (mcells), property, guint64val, NULL);
      break;
    }
    case G_TYPE_INT64:
    {
      gint64 gint64val = atoi (prop_value);
      g_object_set (G_OBJECT (mcells), property, gint64val, NULL);
      break;
    }
    case G_TYPE_FLOAT:
    {
      float floatval = atof (prop_value);
      g_object_set (G_OBJECT (mcells), property, floatval, NULL);
      break;
    }
    case G_TYPE_DOUBLE:
    {
      double doubleval = strtod (prop_value, NULL);
      g_object_set (G_OBJECT (mcells), property, doubleval, NULL);
      break;
    }
    default:
      fprintf (stderr, "You gave me something wrong type of data !!! \n");
      break;
  }
}

// gst-launch v4l2src ! videoscale ! videorate ! capsfilter "caps=video/x-raw-yuv,width=320,height=240,framerate=10/1" ! videoconvert ! motioncells ! videoconvert ! xvimagesink
int
main (int argc, char *argv[])
{
  GstElement *pipeline, *source, *videor, *videos, *decodebin, *capsf,
      *colorsp0, *colorsp1, *mcells, *sink;
  GstCaps *caps;
  gchar property[20];
  gchar prop_value[100];
  GParamSpec **property_specs;
  guint num_properties, i;
  GValue value = { 0, };
  gboolean found_property = FALSE;
  int ret;

  // Initialisation //
  gst_init (&argc, &argv);
  fprintf (stderr, "Usage: %s test or rtsp rtsp://your/cam/address\n", argv[0]);
  // Create gstreamer elements //
  pipeline = gst_pipeline_new ("moitoncells-pipeline");
  if (argc == 2 && (g_strcmp0 (argv[1], "test") == 0))
    source = gst_element_factory_make ("videotestsrc", "vidsrc");
  else if (argc == 3 && (g_strcmp0 (argv[1], "rtsp") == 0))
    source = gst_element_factory_make ("rtspsrc", "rtspsrc0");
  else if (argc == 1)
    source = gst_element_factory_make ("v4l2src", "v4l2");
  else {
    fprintf (stderr, "Usage: %s test or rtsp rtsp://your/cam/address\n",
        argv[0]);
    exit (-1);
  }

  videor = gst_element_factory_make ("videorate", "videor");
  videos = gst_element_factory_make ("videoscale", "videos");
  capsf = gst_element_factory_make ("capsfilter", "capsf");
  if (argc == 3 && (g_strcmp0 (argv[1], "rtsp") == 0))
    decodebin = gst_element_factory_make ("decodebin", "decode");
  else
    decodebin = NULL;
  colorsp0 = gst_element_factory_make ("videoconvert", "colorspace0");
  mcells = gst_element_factory_make ("motioncells", "mcells");
  colorsp1 = gst_element_factory_make ("videoconvert", "colorspace1");
  sink = gst_element_factory_make ("xvimagesink", "xv-image-sink");
  if (!pipeline || !source || !videor || !videos || !capsf || !colorsp0
      || !mcells || !colorsp1 || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }
  if (argc == 3 && (g_strcmp0 (argv[1], "rtsp") == 0) && !decodebin) {
    g_printerr ("Decodebin could not be created. Exiting.\n");
    return -1;
  }
  if ((g_strcmp0 (argv[1], "rtsp") == 0)) {
    g_object_set (G_OBJECT (source), "location", argv[2], NULL);
    g_object_set (G_OBJECT (source), "latency", 1000, NULL);
  } else if ((g_strcmp0 (argv[1], "test") == 0))
    g_object_set (G_OBJECT (source), "pattern", 18, NULL);

  caps =
      gst_caps_from_string
      ("video/x-raw-yuv,width=320,height=240,framerate=10/1");
  g_object_set (G_OBJECT (capsf), "caps", caps, NULL);
  //g_object_set (G_OBJECT (sink), "sync",FALSE,NULL);

  if (argc > 1) {
    if (g_strcmp0 (argv[1], "test") == 0) {
      gst_bin_add_many (GST_BIN (pipeline),
          source, videor, videos, capsf, colorsp0, mcells, colorsp1, sink,
          NULL);

      gst_element_link_many (source, videor, videos, capsf, colorsp0, mcells,
          colorsp1, sink, NULL);
    } else if (g_strcmp0 (argv[1], "rtsp") == 0) {
      gst_bin_add_many (GST_BIN (pipeline),
          source, videor, videos, capsf, decodebin, colorsp0, mcells, colorsp1,
          sink, NULL);

      gst_element_link_many (source, videor, videos, capsf, decodebin, colorsp0,
          mcells, colorsp1, sink, NULL);
    }
  } else {                      //default
    gst_bin_add_many (GST_BIN (pipeline),
        source, videor, videos, capsf, colorsp0, mcells, colorsp1, sink, NULL);

    gst_element_link_many (source, videor, videos, capsf, colorsp0, mcells,
        colorsp1, sink, NULL);
  }

  g_print ("Now playing\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Running...\n");
  g_print ("You can use these properties : \n");
  gst_element_print_properties (mcells);
  g_print ("change property here: example  some_property property_value \n");
  g_print ("Quit with 'q' \n");
  //get all properties
  property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (mcells),
      &num_properties);
  while (TRUE) {
    found_property = FALSE;
    i = 0;

    ret = scanf ("%19s %99s", property, prop_value);

    if (ret < 1)
      g_printerr ("Error parsing command.\n");

    if ((g_strcmp0 (property, "q") == 0) || (g_strcmp0 (prop_value, "q") == 0))
      break;
    printf ("property: %s -> value: %s \n", property, prop_value);
    for (i = 0; i < num_properties; i++) {
      GParamSpec *param = property_specs[i];
      g_value_init (&value, param->value_type);
      g_object_get_property (G_OBJECT (mcells), param->name, &value);
      //fprintf(stderr,"property: %s and param name: %s and property value: %s \n",property,param->name,prop_value);
      if ((g_strcmp0 (property, param->name) == 0) && !found_property &&
          (g_strcmp0 (prop_value, "") != 0)
          && (g_strcmp0 (prop_value, "\"") != 0)
          && (g_strcmp0 (prop_value, "\'") != 0)) {
        GType type;
        found_property = TRUE;
        type = param->value_type;
        setProperty (mcells, property, prop_value, type, &value);
      }
      g_value_unset (&value);
      if (found_property)
        break;
    }
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
