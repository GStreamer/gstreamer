/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Andy Wingo <wingo at pobox dot com>
 *
 * gstsf.c: libsndfile plugin for GStreamer
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


#include <gst/gst.h>
#include <string.h>
#include "gstsf.h"

static GstElementDetails sfsrc_details = {
  "Sndfile Source",
  "Source/Audio",
  "LGPL",
  "Read audio streams from disk using libsndfile",
  VERSION,
  "Andy Wingo <wingo at pobox dot com>",
  "(C) 2003"
};

static GstElementDetails sfsink_details = {
  "Sndfile Sink",
  "Sink/Audio",
  "LGPL",
  "Write audio streams to disk using libsndfile",
  VERSION,
  "Andy Wingo <wingo at pobox dot com>",
  "(C) 2003"
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_MAJOR_TYPE,
  ARG_MINOR_TYPE,
  ARG_LOOP,
  ARG_CREATE_PADS
};

#define GST_SF_BUF_BYTES 2048
#define GST_SF_BUF_FRAMES (GST_SF_BUF_BYTES / sizeof(float))

GST_PAD_TEMPLATE_FACTORY (sf_src_factory,
  "src%d",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sf_src",
    "audio/raw",
    "rate",       GST_PROPS_INT_RANGE (1, G_MAXINT),
    "format",     GST_PROPS_STRING ("float"),
    "layout",     GST_PROPS_STRING ("gfloat"),
    "intercept",  GST_PROPS_FLOAT(0.0),
    "slope",      GST_PROPS_FLOAT(1.0),
    "channels",   GST_PROPS_INT (1)
  )
);

GST_PAD_TEMPLATE_FACTORY (sf_sink_factory,
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sf_sink",
    "audio/raw",
    "rate",       GST_PROPS_INT_RANGE (1, G_MAXINT),
    "format",     GST_PROPS_STRING ("float"),
    "layout",     GST_PROPS_STRING ("gfloat"),
    "intercept",  GST_PROPS_FLOAT(0.0),
    "slope",      GST_PROPS_FLOAT(1.0),
    "channels",   GST_PROPS_INT (1)
  )
);

#define GST_TYPE_SF_MAJOR_TYPES (gst_sf_major_types_get_type())
static GType
gst_sf_major_types_get_type (void) 
{
  static GType sf_major_types_type = 0;
  static GEnumValue *sf_major_types = NULL;
  
  if (!sf_major_types_type) 
  {
    SF_FORMAT_INFO format_info;
    int k, count ;

    sf_command (NULL, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof (int)) ;

    sf_major_types = g_new0 (GEnumValue, count + 1);
    
    for (k = 0 ; k < count ; k++) {
      format_info.format = k ;
      sf_command (NULL, SFC_GET_FORMAT_MAJOR, &format_info, sizeof (format_info));
      sf_major_types[k].value = format_info.format;
      sf_major_types[k].value_name = g_strdup (format_info.name);
      sf_major_types[k].value_nick = g_strdup (format_info.extension);

      /* Irritatingly enough, there exist major_types with the same extension. Let's
         just hope that sndfile gives us the list in alphabetical order, as it
         currently does. */
      if (k > 0 && strcmp (sf_major_types[k].value_nick, sf_major_types[k-1].value_nick) == 0) {
        g_free (sf_major_types[k].value_nick);
        sf_major_types[k].value_nick = g_strconcat (sf_major_types[k-1].value_nick, "-",
                                                    sf_major_types[k].value_name, NULL);
        g_strcanon (sf_major_types[k].value_nick,
                    G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
      }
    }

    sf_major_types_type = g_enum_register_static ("GstSndfileMajorTypes", sf_major_types);
  }
  return sf_major_types_type;
}

#define GST_TYPE_SF_MINOR_TYPES (gst_sf_minor_types_get_type())
static GType
gst_sf_minor_types_get_type (void) 
{
  static GType sf_minor_types_type = 0;
  static GEnumValue *sf_minor_types = NULL;
  
  if (!sf_minor_types_type) 
  {
    SF_FORMAT_INFO format_info;
    int k, count ;

    sf_command (NULL, SFC_GET_FORMAT_SUBTYPE_COUNT, &count, sizeof (int)) ;

    sf_minor_types = g_new0 (GEnumValue, count + 1);
    
    for (k = 0 ; k < count ; k++) {
      format_info.format = k ;
      sf_command (NULL, SFC_GET_FORMAT_SUBTYPE, &format_info, sizeof (format_info));
      sf_minor_types[k].value = format_info.format;
      sf_minor_types[k].value_name = g_strdup (format_info.name);
      sf_minor_types[k].value_nick = g_ascii_strdown (format_info.name, -1);
      g_strcanon (sf_minor_types[k].value_nick, G_CSET_a_2_z G_CSET_DIGITS "-", '-');
    }

    sf_minor_types_type = g_enum_register_static ("GstSndfileMinorTypes", sf_minor_types);
  }
  return sf_minor_types_type;
}

static void	gst_sf_class_init	(GstSFClass *klass);
static void	gst_sf_init		(GstSF *this);

static gboolean gst_sf_open_file 	(GstSF *this);
static void 	gst_sf_close_file 	(GstSF *this);

static void	gst_sf_loop		(GstElement *element);

static void	gst_sf_set_property	(GObject *object, guint prop_id, const GValue *value, 
                                         GParamSpec *pspec);
static void	gst_sf_get_property	(GObject *object, guint prop_id, GValue *value, 
                                         GParamSpec *pspec);

static GstPad*	gst_sf_request_new_pad	(GstElement *element, GstPadTemplate *templ,
                                         const gchar *unused);

static GstElementStateReturn gst_sf_change_state (GstElement *element);
static GstPadLinkReturn gst_sf_link	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;

GType
gst_sf_get_type (void) 
{
  static GType sf_type = 0;

  if (!sf_type) {
    static const GTypeInfo sf_info = {
      sizeof (GstSFClass),      NULL,
      NULL,
      (GClassInitFunc)NULL, /* don't even initialize the class */
      NULL,
      NULL,
      sizeof (GstSF),
      0,
      (GInstanceInitFunc)NULL /* abstract base class */
    };
    sf_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSF", &sf_info, 0);
  }
  return sf_type;
}

GType
gst_sfsrc_get_type (void) 
{
  static GType sfsrc_type = 0;

  if (!sfsrc_type) {
    static const GTypeInfo sfsrc_info = {
      sizeof (GstSFClass),      NULL,
      NULL,
      (GClassInitFunc) gst_sf_class_init,
      NULL,
      NULL,
      sizeof (GstSF),
      0,
      (GInstanceInitFunc) gst_sf_init,
    };
    sfsrc_type = g_type_register_static (GST_TYPE_SF, "GstSFSrc", &sfsrc_info, 0);
  }
  return sfsrc_type;
}

GType
gst_sfsink_get_type (void) 
{
  static GType sfsink_type = 0;

  if (!sfsink_type) {
    static const GTypeInfo sfsink_info = {
      sizeof (GstSFClass),      NULL,
      NULL,
      (GClassInitFunc) gst_sf_class_init,
      NULL,
      NULL,
      sizeof (GstSF),
      0,
      (GInstanceInitFunc) gst_sf_init,
    };
    sfsink_type = g_type_register_static (GST_TYPE_SF, "GstSFSink", &sfsink_info, 0);
  }
  return sfsink_type;
}

static void
gst_sf_class_init (GstSFClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GParamSpec *pspec;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  /* although this isn't really the parent class, that's ok; GstSF doesn't
     override any methods */
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_element_class_install_std_props (gstelement_class, "location",
                                       ARG_LOCATION, G_PARAM_READWRITE, NULL);
  pspec = g_param_spec_enum
    ("major-type", "Major type", "Major output type", GST_TYPE_SF_MAJOR_TYPES,
     SF_FORMAT_WAV, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (gobject_class, ARG_MAJOR_TYPE, pspec);
  pspec = g_param_spec_enum
    ("minor-type", "Minor type", "Minor output type", GST_TYPE_SF_MINOR_TYPES,
     SF_FORMAT_FLOAT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (gobject_class, ARG_MINOR_TYPE, pspec);

  if (G_TYPE_FROM_CLASS (klass) == GST_TYPE_SFSRC) {
    pspec = g_param_spec_boolean ("loop", "Loop?", "Loop the output?",
                                  FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property (gobject_class, ARG_LOOP, pspec);
    pspec = g_param_spec_boolean ("create-pads", "Create pads?", "Create one pad for each channel in the sound file?",
                                  TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property (gobject_class, ARG_CREATE_PADS, pspec);
  }
 
  gobject_class->set_property = gst_sf_set_property;
  gobject_class->get_property = gst_sf_get_property;

  gstelement_class->change_state = gst_sf_change_state;
  gstelement_class->request_new_pad = gst_sf_request_new_pad;
}

static void 
gst_sf_init (GstSF *this) 
{
  gst_element_set_loop_function (GST_ELEMENT (this), gst_sf_loop);
}

static GstPad*
gst_sf_request_new_pad (GstElement *element, GstPadTemplate *templ,
                        const gchar *unused) 
{
  gchar *name;
  GstSF *this;
  GstSFChannel *channel;

  this = GST_SF (element);
  channel = g_new0 (GstSFChannel, 1);
  
  if (templ->direction == GST_PAD_SINK) {
    /* we have an SFSink */
    name = g_strdup_printf ("sink%d", this->channelcount);
    this->numchannels++;
    if (this->file) {
      gst_sf_close_file (this);
      gst_sf_open_file (this);
    }
  } else {
    /* we have an SFSrc */
    name = g_strdup_printf ("src%d", this->channelcount);
  }
  
  channel->pad = gst_pad_new_from_template (templ, name);
  gst_element_add_pad (GST_ELEMENT (this), channel->pad);
  gst_pad_set_link_function (channel->pad, gst_sf_link);
  
  this->channels = g_list_append (this->channels, channel);
  this->channelcount++;
  
  GST_DEBUG (0, "sf added pad %s\n", name);

  g_free (name);
  return channel->pad;
}

static GstPadLinkReturn
gst_sf_link (GstPad *pad, GstCaps *caps)
{
  GstSF *this = (GstSF*)GST_OBJECT_PARENT (pad);
  
  gst_caps_get_int (caps, "rate", &this->rate);

  return GST_PAD_LINK_OK;
}

static void
gst_sf_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSF *this = GST_SF (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (GST_FLAG_IS_SET (object, GST_SF_OPEN))
        gst_sf_close_file (this);
      if (this->filename)
	g_free (this->filename);

      if (g_value_get_string (value))
        this->filename = g_strdup (g_value_get_string (value));
      else
        this->filename = NULL;

      if (this->filename)
        gst_sf_open_file (this);
      break;

    case ARG_MAJOR_TYPE:
      this->format_major = g_value_get_enum (value);
      break;

    case ARG_MINOR_TYPE:
      this->format_subtype = g_value_get_enum (value);
      break;

    case ARG_LOOP:
      this->loop = g_value_get_boolean (value);
      break;

    case ARG_CREATE_PADS:
      this->create_pads = g_value_get_boolean (value);
      if (this->file && this->create_pads) {
        int i;
        for (i=g_list_length (this->channels); i<this->numchannels; i++)
          gst_element_get_request_pad ((GstElement*)this, "src%d");
      }
      break;

    default:
      break;
  }
}

static void   
gst_sf_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSF *this = GST_SF (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, this->filename);
      break;

    case ARG_MAJOR_TYPE:
      g_value_set_enum (value, this->format_major);
      break;

    case ARG_MINOR_TYPE:
      g_value_set_enum (value, this->format_subtype);
      break;

    case ARG_LOOP:
      g_value_set_boolean (value, this->loop);
      break;

    case ARG_CREATE_PADS:
      g_value_set_boolean (value, this->create_pads);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_sf_open_file (GstSF *this)
{
  int mode;
  SF_INFO info;
  
  g_return_val_if_fail (!GST_FLAG_IS_SET (this, GST_SF_OPEN), FALSE);

  if (!this->filename) {
    gst_element_error (GST_ELEMENT (this), "sndfile::location was not set");
    return FALSE;
  }
    
  if (GST_IS_SFSRC (this)) {
    mode = SFM_READ;
    info.format = 0;
  } else {
    mode = SFM_WRITE;
    this->format = this->format_major | this->format_subtype;
    info.samplerate = this->rate;
    info.channels = this->numchannels;
    info.format = this->format;

    if (!sf_format_check (&info)) {
      gst_element_error (GST_ELEMENT (this),
                         g_strdup_printf ("Input parameters (rate:%d, channels:%d, format:%x) invalid",
                                          info.samplerate, info.channels, info.format));
      return FALSE;
    }
  }

  this->file = sf_open (this->filename, mode, &info);

  if (!this->file) {
    gst_element_error (GST_ELEMENT (this),
                       g_strdup_printf ("could not open file \"%s\": %s",
                                        this->filename, sf_strerror (NULL)));
    return FALSE;
  }

  if (GST_IS_SFSRC (this)) {
    GList *l = NULL;
    /* the number of channels in the file can be different than the number of
     * pads */
    this->numchannels = info.channels;
    this->rate = info.samplerate;

    if (this->create_pads) {
      int i;
      for (i=g_list_length (this->channels); i<this->numchannels; i++)
        gst_element_get_request_pad ((GstElement*)this, "src%d");
    }
  
    for (l=this->channels; l; l=l->next)
      /* queue the need to set caps */
      GST_SF_CHANNEL (l)->caps_set = FALSE;
  }

  this->buffer = g_malloc (this->numchannels * GST_SF_BUF_BYTES);
  GST_FLAG_SET (this, GST_SF_OPEN);

  return TRUE;
}

static void
gst_sf_close_file (GstSF *this)
{
  int err = 0;

  g_return_if_fail (GST_FLAG_IS_SET (this, GST_SF_OPEN));

  if ((err = sf_close (this->file)))
    gst_element_error (GST_ELEMENT (this),
                       g_strdup_printf ("sndfile: could not close file \"%s\": %s",
                                        this->filename, sf_error_number (err)));
  else
    GST_FLAG_UNSET (this, GST_SF_OPEN);

  this->file = NULL;
  if (this->buffer)
    g_free (this->buffer);
  this->buffer = NULL;
}

static void 
gst_sf_loop (GstElement *element) 
{
  GstSF *this;
  GList *l = NULL;

  this = (GstSF*)element;
  
  if (this->channels == NULL) {
    gst_element_error (element, "You must connect at least one pad to soundfile elements.");
    return;
  }
  if (!GST_FLAG_IS_SET (this, GST_SF_OPEN))
    if (!gst_sf_open_file (this))
      return; /* we've already set gst_element_error */

  if (GST_IS_SFSRC (this)) {
    sf_count_t read;
    gint i, j;
    int eos = 0;
    int nchannels = this->numchannels;
    GstSFChannel *channel = NULL;
    gfloat *data;
    gfloat *buf = this->buffer;
    GstBuffer *out;

    read = sf_readf_float (this->file, buf, GST_SF_BUF_FRAMES);
    if (read < GST_SF_BUF_FRAMES)
      eos = 1;

    if (read)
      for (i=0,l=this->channels; l; l=l->next,i++) {
        channel = GST_SF_CHANNEL (l);

        /* don't push on disconnected pads -- useful for ::create-pads=TRUE*/
        if (!GST_PAD_PEER (channel->pad))
          continue;

        if (!channel->caps_set) {
          GstCaps *caps = GST_PAD_CAPS (GST_SF_CHANNEL (l)->pad);
          if (!caps)
            caps = gst_caps_copy
              (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (GST_SF_CHANNEL (l)->pad)));
          gst_caps_set (caps, "rate", GST_PROPS_INT (this->rate), NULL);
          /* we know it's fixed, yo. */
          GST_CAPS_FLAG_SET (caps, GST_CAPS_FIXED);
          if (!gst_pad_try_set_caps (GST_SF_CHANNEL (l)->pad, caps)) {
            gst_element_error (GST_ELEMENT (this),
                               g_strdup_printf ("Opened file with sample rate %d, but could not set caps",
                                                this->rate));
            sf_close (this->file);
            this->file = NULL;
            g_free (this->buffer);
            this->buffer = NULL;
            return;
          }
          channel->caps_set = TRUE;
        }

        out = gst_buffer_new_and_alloc (read * sizeof(float));
        data = (gfloat*)GST_BUFFER_DATA (out);
        for (j=0; j<read; j++)
          data[j] = buf[j * nchannels + i % nchannels];
        gst_pad_push (channel->pad, out);
      }

    if (eos) {
      if (this->loop) {
        sf_seek (this->file, (sf_count_t)0, SEEK_SET);
        eos = 0;
      } else {
        for (l=this->channels; l; l=l->next)
          gst_pad_push (GST_SF_CHANNEL (l)->pad, gst_event_new (GST_EVENT_EOS));
        gst_element_set_eos (element);
      }
    }
  } else {
    /* unimplemented */
  }
}

static GstElementStateReturn
gst_sf_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_SF (element), GST_STATE_FAILURE);

  /* if going to NULL then close the file */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) 
    if (GST_FLAG_IS_SET (element, GST_SF_OPEN))
      gst_sf_close_file (GST_SF (element));

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  
  factory = gst_element_factory_new ("sfsrc", GST_TYPE_SFSRC,
                                     &sfsrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sf_src_factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  
  factory = gst_element_factory_new ("sfsink", GST_TYPE_SFSINK,
                                     &sfsink_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sf_sink_factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstsf",
  plugin_init
};
