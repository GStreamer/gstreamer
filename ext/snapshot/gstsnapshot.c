/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "config.h"
#endif
#include <string.h>
#include <inttypes.h>

#include "gstsnapshot.h"
#include <gst/video/video.h>

#define MAX_HEIGHT	2048

/* elementfactory information */
static GstElementDetails snapshot_details = {
  "snapshot",
  "Filter/Editor/Video",
  "Dump a frame to a png file",
  "Jeremy SIMON <jsimon13@yahoo.fr>",
};

static GstStaticPadTemplate snapshot_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR)
);

static GstStaticPadTemplate snapshot_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR)
);

/* Snapshot signals and args */
enum {
  /* FILL ME */
  SNAPSHOT_SIGNAL,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FRAME,
  ARG_LOCATION
};

static GType 	gst_snapshot_get_type 	(void);
static void     gst_snapshot_base_init  (gpointer g_class);
static void	gst_snapshot_class_init	(GstSnapshotClass *klass);
static void	gst_snapshot_init	(GstSnapshot *snapshot);

static void	gst_snapshot_chain	(GstPad *pad, GstData *_data);

static void	gst_snapshot_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_snapshot_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void 	snapshot_handler(GstElement *element);


static GstElementClass *parent_class = NULL;
static guint gst_snapshot_signals[LAST_SIGNAL] = { 0 }; 


static void user_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
  g_warning("%s", error_msg);
}

static void user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning("%s", warning_msg);
}

GType
gst_snapshot_get_type (void)
{
  static GType snapshot_type = 0;

  if (!snapshot_type) {
    static const GTypeInfo snapshot_info = {
      sizeof(GstSnapshotClass),
      gst_snapshot_base_init,
      NULL,
      (GClassInitFunc)gst_snapshot_class_init,
      NULL,
      NULL,
      sizeof(GstSnapshot),
      0,
      (GInstanceInitFunc)gst_snapshot_init,
    };
    snapshot_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSnapshot", &snapshot_info, 0);
  }
  return snapshot_type;
}

static void
gst_snapshot_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&snapshot_sink_factory));
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&snapshot_src_factory));

  gst_element_class_set_details (element_class, &snapshot_details);
}

static void
gst_snapshot_class_init (GstSnapshotClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAME,
    g_param_spec_long("frame","frame","frame",
                         0, G_MAXLONG, 0, G_PARAM_READWRITE)); 
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOCATION,
    g_param_spec_string("location","location","location",
                         0,G_PARAM_READWRITE)); 
	
  gst_snapshot_signals[SNAPSHOT_SIGNAL] =
	      g_signal_new("snapshot", G_TYPE_FROM_CLASS(klass),
	 	            G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(GstSnapshotClass, snapshot),
		            NULL, NULL, g_cclosure_marshal_VOID__VOID,  G_TYPE_NONE, 0);

  klass->snapshot = snapshot_handler;

  gobject_class->set_property = gst_snapshot_set_property;
  gobject_class->get_property = gst_snapshot_get_property;
}

static void
snapshot_handler(GstElement *element)
{
  GstSnapshot *snapshot;

  snapshot = GST_SNAPSHOT( element );
  snapshot->snapshot_asked=TRUE;
}


static gboolean
gst_snapshot_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstSnapshot *filter;
  gdouble fps;
  GstStructure *structure;

  filter = GST_SNAPSHOT (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int  (structure, "width", &filter->width);
  gst_structure_get_int  (structure, "height", &filter->height);
  gst_structure_get_double  (structure, "framerate", &fps);
  gst_structure_get_fourcc  (structure, "format", &filter->format);
  filter->to_bpp = 24;

  filter->png_struct_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, user_error_fn, user_warning_fn);
  if ( filter->png_struct_ptr == NULL )
    g_warning( "Failed to initialize png structure");

  filter->png_info_ptr = png_create_info_struct( filter->png_struct_ptr );

  if (setjmp( filter->png_struct_ptr->jmpbuf)) 
    png_destroy_write_struct(&filter->png_struct_ptr, &filter->png_info_ptr);
      
  gst_pad_try_set_caps (filter->srcpad, caps);

  return GST_PAD_LINK_OK;
}

static void
gst_snapshot_init (GstSnapshot *snapshot)
{
  snapshot->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get (&snapshot_sink_factory), "sink");
  gst_pad_set_link_function (snapshot->sinkpad, gst_snapshot_sinkconnect);
  gst_pad_set_chain_function (snapshot->sinkpad, gst_snapshot_chain);
  gst_element_add_pad (GST_ELEMENT (snapshot), snapshot->sinkpad);

  snapshot->srcpad = gst_pad_new_from_template (gst_static_pad_template_get (&snapshot_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (snapshot), snapshot->srcpad);

  snapshot->cur_frame = 0;
  snapshot->frame=-1;
  snapshot->snapshot_asked=FALSE;
}


static void
gst_snapshot_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSnapshot *snapshot;
  guchar *data;
  gulong size;
  gint i;
  png_byte *row_pointers[ MAX_HEIGHT ];
  FILE *fp;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  snapshot = GST_SNAPSHOT (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  GST_DEBUG ("snapshot: have buffer of %d\n", GST_BUFFER_SIZE(buf));

  snapshot->cur_frame++;
  if (snapshot->cur_frame == snapshot->frame ||
      snapshot->snapshot_asked == TRUE )
  {
    snapshot->snapshot_asked = FALSE;

    GST_INFO ("dumpfile : %s\n", snapshot->location );
    fp = fopen( snapshot->location, "wb" );
    if ( fp == NULL )
      g_warning(" Can not open %s\n", snapshot->location );
    else
    {
      png_set_filter( snapshot->png_struct_ptr, 0, PNG_FILTER_NONE  | PNG_FILTER_VALUE_NONE );
      png_init_io(snapshot->png_struct_ptr, fp);
      png_set_compression_level( snapshot->png_struct_ptr, 9);
      png_set_IHDR(
       snapshot->png_struct_ptr,
       snapshot->png_info_ptr,
       snapshot->width,
       snapshot->height,
       snapshot->to_bpp/3,
       PNG_COLOR_TYPE_RGB,
       PNG_INTERLACE_NONE,
       PNG_COMPRESSION_TYPE_DEFAULT,
       PNG_FILTER_TYPE_DEFAULT
      );
  
     for ( i = 0; i < snapshot->height; i++ )
       row_pointers[i] = data + (snapshot->width * i * snapshot->to_bpp/8 );
     
     png_write_info( snapshot->png_struct_ptr, snapshot->png_info_ptr );
     png_write_image( snapshot->png_struct_ptr, row_pointers );
     png_write_end( snapshot->png_struct_ptr, NULL );
     png_destroy_info_struct (  snapshot->png_struct_ptr, &snapshot->png_info_ptr );
     png_destroy_write_struct( &snapshot->png_struct_ptr, (png_infopp)NULL );
     fclose( fp );
     g_signal_emit (G_OBJECT (snapshot),
		    gst_snapshot_signals[SNAPSHOT_SIGNAL], 0);


    }
  }

  gst_pad_push(snapshot->srcpad,GST_DATA (buf ));
}

static void
gst_snapshot_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSnapshot *snapshot;

  g_return_if_fail(GST_IS_SNAPSHOT(object));
  snapshot = GST_SNAPSHOT(object);

  switch (prop_id) {
    case ARG_LOCATION:
      snapshot->location = g_strdup(g_value_get_string (value));
      break;
    case ARG_FRAME:
      snapshot->frame = g_value_get_long(value);
      break;
    default:
      break;
  }
}

static void
gst_snapshot_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSnapshot *snapshot;

  g_return_if_fail(GST_IS_SNAPSHOT(object));
  snapshot = GST_SNAPSHOT(object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string(value, snapshot->location);
      break;
    case ARG_FRAME:
      g_value_set_long(value, snapshot->frame);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "snapshot", GST_RANK_NONE, GST_TYPE_SNAPSHOT))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "snapshot",
  "Dump a frame to a png file",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)
