//#define RGB

/* Gnome-Streamer
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

#include <string.h>

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */
#include "gstdvdec.h"

#define NTSC

#ifdef NTSC
#  define HEIGHT 480
#  define BUFFER 120000
#else
#  define HEIGHT 576
#  define BUFFER 144000
#endif

/* The ElementDetails structure gives a human-readable description
 * of the plugin, as well as author and version data.
 */
static GstElementDetails dvdec_details = {
  "DV (smpte314) decoder plugin",
  "Decoder/DV",
  "Uses libdv to decode DV video (libdv.sourceforge.net)",
  VERSION,
  "asdfasdf",
  "(C) 2001",
};

/* These are the signals that this element can fire.  They are zero-
 * based because the numbers themselves are private to the object.
 * LAST_SIGNAL is used for initialization of the signal array.
 */
enum {
  ASDF,
  /* FILL ME */
  LAST_SIGNAL
};

/* Arguments are identified the same way, but cannot be zero, so you
 * must leave the ARG_0 entry in as a placeholder.
 */
enum {
  ARG_0,
  /* FILL ME */
};

/* The PadFactory structures describe what pads the element has or
 * can have.  They can be quite complex, but for this dvdec plugin
 * they are rather simple.
 */
GST_PADTEMPLATE_FACTORY ( sink_temp,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "dv_dec_sink",
    "video/dv",
    "format",   GST_PROPS_STRING ("NTSC")
  )
)


#ifdef RGB
GST_PADTEMPLATE_FACTORY ( video_src_temp,
  "video",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "dv_dec_src",
    "video/raw",
    "format", 		GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
    "bpp", 		GST_PROPS_INT(24),
    "depth", 		GST_PROPS_INT(24),
    "red_mask", 	GST_PROPS_INT(0x0000ff),
    "green_mask", 	GST_PROPS_INT(0x00ff00),
    "blue_mask", 	GST_PROPS_INT(0xff0000),
    "width",     	GST_PROPS_INT (720),
    "height",    	GST_PROPS_INT (HEIGHT)
  )
)
#else
GST_PADTEMPLATE_FACTORY ( video_src_temp,
  "video",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "dv_dec_src",
    "video/raw",
    "format",    GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')),
    "width",     GST_PROPS_INT (720),
    "height",    GST_PROPS_INT (HEIGHT)
  )
)
#endif

GST_PADTEMPLATE_FACTORY ( audio_src_temp,
  "audio",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "arts_sample",
    "audio/raw",
    "format",   	GST_PROPS_STRING ("int"),
    "law",      	GST_PROPS_INT (0),
    "depth",   		GST_PROPS_INT (16),
    "width",    	GST_PROPS_INT (16),
    "signed",   	GST_PROPS_BOOLEAN (TRUE),
    "channels", 	GST_PROPS_INT (2),
    "endianness", 	GST_PROPS_INT (G_LITTLE_ENDIAN)
  )
)


/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_dvdec_class_init	(GstDVDecClass *klass);
static void	gst_dvdec_init	(GstDVDec *dvdec);

static void	gst_dvdec_loop		(GstElement *element);

static void	gst_dvdec_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_dvdec_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* The parent class pointer needs to be kept around for some object
 * operations.
 */
static GstElementClass *parent_class = NULL;

/* This array holds the ids of the signals registered for this object.
 * The array indexes are based on the enum up above.
 */
static guint gst_dvdec_signals[LAST_SIGNAL] = { 0 };

/* This function is used to register and subsequently return the type
 * identifier for this object class.  On first invocation, it will
 * register the type, providing the name of the class, struct sizes,
 * and pointers to the various functions that define the class.
 */
GType
gst_dvdec_get_type(void)
{
  static GType dvdec_type = 0;

  if (!dvdec_type) {
    static const GTypeInfo dvdec_info = {
      sizeof(GstDVDecClass),      NULL,      NULL,
      (GClassInitFunc)gst_dvdec_class_init,
      NULL,
      NULL,
      sizeof(GstDVDec),
      0,
      (GInstanceInitFunc)gst_dvdec_init,
    };
    dvdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstDVDec", &dvdec_info, 0);
  }
  return dvdec_type;
}

/* In order to create an instance of an object, the class must be
 * initialized by this function.  GObject will take care of running
 * it, based on the pointer to the function provided above.
 */
static void
gst_dvdec_class_init (GstDVDecClass *klass)
{
  /* Class pointers are needed to supply pointers to the private
   * implementations of parent class methods.
   */
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  /* Since the dvdec class contains the parent classes, you can simply
   * cast the pointer to get access to the parent classes.
   */
  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  /* The parent class is needed for class method overrides. */
  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  /* Here we add an argument to the object.  This argument is an integer,
   * and can be both read and written.
   */
//  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ACTIVE,
//    g_param_spec_int("active","active","active",
//                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME

  /* Here we add a signal to the object. This is avery useless signal
   * called asdf. The signal will also pass a pointer to the listeners
   * which happens to be the dvdec element itself */
  gst_dvdec_signals[ASDF] =
    g_signal_new("asdf", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstDVDecClass, asdf), NULL, NULL,
                   g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                   GST_TYPE_DVDEC);

  /* The last thing is to provide the functions that implement get and set
   * of arguments.
   */
  gobject_class->set_property = gst_dvdec_set_property;
  gobject_class->get_property = gst_dvdec_get_property;


  // table initialization, only do once
  dv_init();
}

/* This function is responsible for initializing a specific instance of
 * the plugin.
 */
static void
gst_dvdec_init(GstDVDec *dvdec)
{
  dvdec->sinkpad = gst_pad_new_from_template (GST_PADTEMPLATE_GET(sink_temp), "sink");
  gst_element_add_pad(GST_ELEMENT(dvdec),dvdec->sinkpad);

  dvdec->videosrcpad = gst_pad_new_from_template (GST_PADTEMPLATE_GET(video_src_temp), "video");
  //gst_pad_set_caps (dvdec->videosrcpad, gst_pad_get_padtemplate_caps (dvdec->videosrcpad));
  gst_element_add_pad(GST_ELEMENT(dvdec),dvdec->videosrcpad);

//  dvdec->audiosrcpad = gst_pad_new_from_template (GST_PADTEMPLATE_GET(audio_src_temp), "audio");
//  gst_pad_set_caps (dvdec->audiosrcpad, gst_pad_get_padtemplate_caps (dvdec->audiosrcpad));
//  gst_element_add_pad(GST_ELEMENT(dvdec),dvdec->audiosrcpad);

  gst_element_set_loop_function (GST_ELEMENT(dvdec), gst_dvdec_loop);

  dvdec->decoder = dv_decoder_new();
  dvdec->decoder->quality = DV_QUALITY_BEST;
  dvdec->inframe = g_malloc(BUFFER);
  dvdec->pool = NULL;
  dvdec->carryover = NULL;
  dvdec->remaining = 0;
}


static void
gst_dvdec_loop (GstElement *element)
{   
  GstDVDec *dvdec;
  int needed;
  GstBuffer *buf;
  guint8 *outframe;
  guint8 *outframe_ptrs[3];
  gint outframe_pitches[3];

  dvdec = GST_DVDEC (element);

  // grab an input frame
  needed = BUFFER;
  if (dvdec->remaining > 0) {
    memcpy(&dvdec->inframe[BUFFER-needed],
           GST_BUFFER_DATA(dvdec->carryover)+(GST_BUFFER_SIZE(dvdec->carryover)-dvdec->remaining),
           dvdec->remaining);
    dvdec->remaining = 0;
    gst_buffer_unref(dvdec->carryover);
  }
  while (needed) {
    buf = gst_pad_pull(dvdec->sinkpad);
    if (needed < GST_BUFFER_SIZE(buf)) {
      memcpy(&dvdec->inframe[BUFFER-needed],GST_BUFFER_DATA(buf),needed);
/**** NOTE: this is done because 1394src doesn't allow buffers to outlive the handler *****/
      dvdec->carryover = gst_buffer_copy(buf);
      dvdec->remaining = GST_BUFFER_SIZE(buf) - needed;
      needed = 0;
    } else {
      memcpy(&dvdec->inframe[BUFFER-needed],GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
      needed -= GST_BUFFER_SIZE(buf);
    }
    gst_buffer_unref(buf);
  }

  if (!GST_PAD_CAPS (dvdec->videosrcpad)) {
    gst_pad_set_caps (dvdec->videosrcpad, gst_pad_get_padtemplate_caps (dvdec->videosrcpad));
  }

  if (!dvdec->pool) {
    dvdec->pool = gst_pad_get_bufferpool (dvdec->videosrcpad);
  }

  buf = NULL;
  if (dvdec->pool) {
    buf = gst_buffer_new_from_pool (dvdec->pool, 0, 0);
  }

  if (!buf) {
    // allocate an output frame
    buf = gst_buffer_new();
#ifdef RGB
    GST_BUFFER_SIZE(buf) = (720*HEIGHT)*3;
#else
    GST_BUFFER_SIZE(buf) = (720*HEIGHT)*2;
#endif
    GST_BUFFER_DATA(buf) = g_malloc(GST_BUFFER_SIZE(buf));
    outframe = GST_BUFFER_DATA(buf);
  } else {
    outframe = GST_BUFFER_DATA (buf);
  }

  outframe_ptrs[0] = outframe;
  outframe_ptrs[1] = outframe_ptrs[0] + 720*HEIGHT;
  outframe_ptrs[2] = outframe_ptrs[1] + 360*HEIGHT;
#ifdef RGB
  outframe_pitches[0] = 720*3;
#else
  outframe_pitches[0] = 720*2;	// huh?
#endif
  outframe_pitches[1] = HEIGHT/2;
  outframe_pitches[2] = HEIGHT/2;

  // now we start decoding the frame
  dv_parse_header(dvdec->decoder,dvdec->inframe);

#ifdef RGB
  dv_decode_full_frame(dvdec->decoder,dvdec->inframe,e_dv_color_rgb,outframe_ptrs,outframe_pitches);
#else
  dv_decode_full_frame(dvdec->decoder,dvdec->inframe,e_dv_color_yuv,outframe_ptrs,outframe_pitches);
#endif

  gst_pad_push(dvdec->videosrcpad,buf);
}



/* Arguments are part of the Gtk+ object system, and these functions
 * enable the element to respond to various arguments.
 */
static void
gst_dvdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDVDec *dvdec;

  /* It's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DVDEC(object));

  /* Get a pointer of the right type. */
  dvdec = GST_DVDEC(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    default:
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_dvdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDVDec *dvdec;

  /* It's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DVDEC(object));
  dvdec = GST_DVDEC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* This is the entry into the plugin itself.  When the plugin loads,
 * this function is called to register everything that the plugin provides.
 */
static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* We need to create an ElementFactory for each element we provide.
   * This consists of the name of the element, the GType identifier,
   * and a pointer to the details structure at the top of the file.
   */
  factory = gst_elementfactory_new("dvdec", GST_TYPE_DVDEC, &dvdec_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* The pad templates can be easily generated from the factories above,
   * and then added to the list of padtemplates for the elementfactory.
   * Note that the generated padtemplates are stored in static global
   * variables, for the gst_dvdec_init function to use later on.
   */
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET(sink_temp));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET(video_src_temp));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET(audio_src_temp));

  /* The very last thing is to register the elementfactory with the plugin. */
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dvdec",
  plugin_init
};

