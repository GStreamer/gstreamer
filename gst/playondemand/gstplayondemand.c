/* GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstplayondemand.h"


#define POD_MAX_PLAYS     192     /* maximum number of simultaneous plays */
#define POD_GSTBUFSIZE    4096    /* gstreamer buffer size to make if no
                                     bufferpool is available, must be divisible
                                     by sizeof(gfloat) */
#define POD_BUFSPERCHUNK  6       /* number of buffers to allocate per chunk in
                                     sink buffer pool */
#define POD_BUFFER_SIZE   882000  /* enough space for 10 seconds of 16-bit audio
                                     at 44100 samples per second ... */

static GstElementDetails play_on_demand_details = {
  "Play On Demand",
  "Filter/Effect",
  "Plays a stream whenever it receives a certain signal",
  VERSION,
  "Leif Morgan Johnson <lmjohns3@eos.ncsu.edu>",
  "(C) 2001",
};


/* Filter signals and args */
enum {
  /* FILL ME */
  PLAY_SIGNAL,
  RESET_SIGNAL,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SILENT,
  ARG_FOLLOWTAIL,
  ARG_BUFFERSIZE
};

static GstPadTemplate*                    
play_on_demand_sink_factory (void)            
{                                         
  static GstPadTemplate *template = NULL; 
                                          
  if (!template) {                        
    template = gst_padtemplate_new 
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append(gst_caps_new ("sink_int",  "audio/raw",
                                    GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
                      gst_caps_new ("sink_float", "audio/raw",
                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS)),
      NULL);
  }                                       
  return template;                        
}

static GstPadTemplate*
play_on_demand_src_factory (void)
{
  static GstPadTemplate *template = NULL;
  
  if (!template)
    template = gst_padtemplate_new 
      ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
       gst_caps_append (gst_caps_new ("src_float", "audio/raw",
                                      GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                        gst_caps_new ("src_int", "audio/raw",
                                      GST_AUDIO_INT_PAD_TEMPLATE_PROPS)),
       NULL);
  
  return template;
}

static void		play_on_demand_class_init	 (GstPlayOnDemandClass *klass);
static void		play_on_demand_init		 (GstPlayOnDemand *filter);

static void		play_on_demand_set_property	 (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		play_on_demand_get_property      (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gint             play_on_demand_parse_caps        (GstPlayOnDemand *filter, GstCaps *caps);

static void		play_on_demand_loop              (GstElement *elem);

static void             play_on_demand_play_handler      (GstElement *elem);
static void             play_on_demand_reset_handler     (GstElement *elem);

static GstElementClass *parent_class = NULL;
static guint gst_pod_filter_signals[LAST_SIGNAL] = { 0 };

static GstBufferPool*
play_on_demand_get_bufferpool (GstPad *pad)
{
  GstPlayOnDemand *filter;

  filter = GST_PLAYONDEMAND(gst_pad_get_parent(pad));

  return gst_pad_get_bufferpool(filter->srcpad);
}

/*
static GstPadNegotiateReturn
play_on_demand_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstPlayOnDemand* filter = GST_PLAYONDEMAND(gst_pad_get_parent(pad));
  
  if (*caps == NULL) 
    return GST_PAD_NEGOTIATE_FAIL;
  
  if (play_on_demand_parse_caps(filter, *caps))
    return GST_PAD_NEGOTIATE_FAIL;
  
  return gst_pad_negotiate_proxy(pad, filter->sinkpad, caps);
}

static GstPadNegotiateReturn
play_on_demand_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstPlayOnDemand* filter = GST_PLAYONDEMAND(gst_pad_get_parent(pad));
  
  if (*caps == NULL) 
    return GST_PAD_NEGOTIATE_FAIL;
  
  if (play_on_demand_parse_caps(filter, *caps))
    return GST_PAD_NEGOTIATE_FAIL;
  
  return gst_pad_negotiate_proxy(pad, filter->srcpad, caps);
}	
*/

static gint
play_on_demand_parse_caps (GstPlayOnDemand *filter, GstCaps *caps)
{
  const gchar *format;
  
  g_return_val_if_fail(filter != NULL, -1);
  g_return_val_if_fail(caps   != NULL, -1);
  
  format = gst_caps_get_string(caps, "format");
  
  filter->rate       = gst_caps_get_int(caps, "rate");
  filter->channels   = gst_caps_get_int(caps, "channels");
  
  if (strcmp(format, "int") == 0) {
    filter->format     = GST_PLAYONDEMAND_FORMAT_INT;
    filter->width      = gst_caps_get_int(caps, "width");
    filter->depth      = gst_caps_get_int(caps, "depth");
    filter->law        = gst_caps_get_int(caps, "law");
    filter->endianness = gst_caps_get_int(caps, "endianness");
    filter->is_signed  = gst_caps_get_int(caps, "signed");
    if (!filter->silent) {
      g_print ("PlayOnDemand : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("PlayOnDemand : format int, bit width %d, endianness %d, signed %s\n",
               filter->width, filter->endianness, filter->is_signed ? "yes" : "no");
    }
  } else if (strcmp(format, "float")==0) {
    filter->format     = GST_PLAYONDEMAND_FORMAT_FLOAT;
    filter->layout     = gst_caps_get_string(caps, "layout");
    filter->intercept  = gst_caps_get_float(caps, "intercept");
    filter->slope      = gst_caps_get_float(caps, "slope");
    if (!filter->silent) {
      g_print ("PlayOnDemand : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("PlayOnDemand : format float, layout %s, intercept %f, slope %f\n",
               filter->layout, filter->intercept, filter->slope);
    }
  } else  {
    return -1;
  }
  return 0;
}


GType
gst_play_on_demand_get_type(void) {
  static GType play_on_demand_type = 0;

  if (! play_on_demand_type) {
    static const GTypeInfo play_on_demand_info = {
      sizeof(GstPlayOnDemandClass),
      NULL,
      NULL,
      (GClassInitFunc) play_on_demand_class_init,
      NULL,
      NULL,
      sizeof(GstPlayOnDemand),
      0,
      (GInstanceInitFunc) play_on_demand_init,
    };
    play_on_demand_type = g_type_register_static(GST_TYPE_ELEMENT, "GstPlayOnDemand", &play_on_demand_info, 0);
  }
  return play_on_demand_type;
}

static void
play_on_demand_class_init (GstPlayOnDemandClass *klass)
{
  GObjectClass    *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class    = (GObjectClass *)    klass;
  gstelement_class = (GstElementClass *) klass;

  gst_pod_filter_signals[PLAY_SIGNAL] =
    g_signal_new("play",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstPlayOnDemandClass, play),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  gst_pod_filter_signals[RESET_SIGNAL] =
    g_signal_new("reset",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstPlayOnDemandClass, reset),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  klass->play  = play_on_demand_play_handler;
  klass->reset = play_on_demand_reset_handler;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         TRUE, G_PARAM_READWRITE)); // CHECKME

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FOLLOWTAIL,
    g_param_spec_boolean("follow-stream-tail","follow-stream-tail","follow-stream-tail",
                         FALSE, G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFERSIZE,
    g_param_spec_uint("buffer-size","buffer-size","buffer-size",
                      0, G_MAXUINT - 1, POD_BUFFER_SIZE, G_PARAM_READWRITE));

  gobject_class->set_property = play_on_demand_set_property;
  gobject_class->get_property = play_on_demand_get_property;
}

static void
play_on_demand_init (GstPlayOnDemand *filter)
{
  guint i;
  
  filter->sinkpad = gst_pad_new_from_template(play_on_demand_sink_factory(), "sink");
  //gst_pad_set_negotiate_function(filter->sinkpad, play_on_demand_negotiate_sink);
  gst_pad_set_bufferpool_function(filter->sinkpad, play_on_demand_get_bufferpool);

  filter->srcpad = gst_pad_new_from_template(play_on_demand_src_factory(), "src");
  //gst_pad_set_negotiate_function(filter->srcpad, play_on_demand_negotiate_src);
  
  gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

  gst_element_set_loop_function(GST_ELEMENT(filter), play_on_demand_loop);

  filter->sinkpool = gst_buffer_pool_get_default(POD_GSTBUFSIZE, POD_BUFSPERCHUNK);

  filter->follow_stream_tail = FALSE;  
  filter->silent      = TRUE;

  filter->buffer      = g_new(gchar, POD_BUFFER_SIZE);
  filter->buffer_size = POD_BUFFER_SIZE;
  filter->start       = 0;
  filter->write       = 0;

  filter->eos         = FALSE;

  /* the plays are stored as an array of buffer offsets. this initializes the
     array to `blank' values (G_MAXUINT is an invalid index for this filter). */
  filter->plays  = g_new(guint, POD_MAX_PLAYS);
  for (i = 0; i < POD_MAX_PLAYS; i++) {
    filter->plays[i] = G_MAXUINT;
  }
}

static void
play_on_demand_loop (GstElement *elem)
{
  GstPlayOnDemand *filter = GST_PLAYONDEMAND(elem);
  guint            num_in, num_out, num_filter;
  GstBuffer       *in, *out;
  register guint   j, k, t;
  guint            w, offset;
  
  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_PLAYONDEMAND(filter));

  filter->srcpool = gst_pad_get_bufferpool(filter->srcpad);

  in = gst_pad_pull(filter->sinkpad);

  if (filter->format == GST_PLAYONDEMAND_FORMAT_INT) {
    if (filter->width == 16) {
      gint16 min = -32768;
      gint16 max = 32767;
      gint16 zero = 0;
#define _TYPE_ gint16
#include "filter.func"
#undef _TYPE_
    } else if (filter->width == 8) {
      gint8 min = -128;
      gint8 max = 127;
      gint8 zero = 0;
#define _TYPE_ gint8
#include "filter.func"
#undef _TYPE_
    }
  } else if (filter->format == GST_PLAYONDEMAND_FORMAT_FLOAT) {
    gfloat min = -1.0;
    gfloat max = 1.0;
    gfloat zero = 0.0;
#define _TYPE_ gfloat
#include "filter.func"
#undef _TYPE_
  }
}

static void
play_on_demand_play_handler(GstElement *elem)
{
  GstPlayOnDemand *filter = GST_PLAYONDEMAND(elem);
  register guint i;

  for (i = 0; i < POD_MAX_PLAYS; i++) {
    if (filter->plays[i] == G_MAXUINT) {
      filter->plays[i] = filter->start;
      break;
    }
  }
}

static void
play_on_demand_reset_handler(GstElement *elem)
{
  GstPlayOnDemand *filter = GST_PLAYONDEMAND(elem);
  register guint i;
  
  for (i = 0; i < POD_MAX_PLAYS; i++) {
    filter->plays[i] = G_MAXUINT;
  }

  filter->start = 0;
  filter->write = 0;
}

static void
play_on_demand_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstPlayOnDemand *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PLAYONDEMAND(object));
  filter = GST_PLAYONDEMAND(object);

  switch (prop_id) {
  case ARG_BUFFERSIZE:
    filter->buffer_size = g_value_get_uint(value);

    /* reallocate space for the buffer with the new size values. */
    g_free(filter->buffer);
    filter->buffer = g_new(gchar, filter->buffer_size);

    /* reset the play pointers and read/write indexes. */
    play_on_demand_reset_handler(GST_ELEMENT(filter));
    break;
  case ARG_SILENT:
    filter->silent = g_value_get_boolean(value);
    break;
  case ARG_FOLLOWTAIL:
    filter->follow_stream_tail = g_value_get_boolean(value);
    play_on_demand_reset_handler(GST_ELEMENT(filter));
    break;
  default:
    break;
  }
}

static void
play_on_demand_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstPlayOnDemand *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PLAYONDEMAND(object));
  filter = GST_PLAYONDEMAND(object);

  switch (prop_id) {
  case ARG_BUFFERSIZE:
    g_value_set_uint(value, filter->buffer_size);
    break;
  case ARG_SILENT:
    g_value_set_boolean(value, filter->silent);
    break;
  case ARG_FOLLOWTAIL:
    g_value_set_boolean(value, filter->follow_stream_tail);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("playondemand",
                                   GST_TYPE_PLAYONDEMAND,
                                   &play_on_demand_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate(factory, play_on_demand_src_factory());
  gst_elementfactory_add_padtemplate(factory, play_on_demand_sink_factory());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE(factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "playondemand",
  plugin_init
};
