/* GStreamer libsndfile plugin
 * Copyright (C) 2003 Andy Wingo <wingo at pobox dot com>
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

#include "gst/gst-i18n-plugin.h"
#include <string.h>
#include <gst/gst.h>

#include <gst/audio/audio.h>

#include "gstsf.h"


static GstElementDetails sfsrc_details = {
  "Sndfile Source",
  "Source/Audio",
  "Read audio streams from disk using libsndfile",
  "Andy Wingo <wingo at pobox dot com>",
};

static GstElementDetails sfsink_details = {
  "Sndfile Sink",
  "Sink/Audio",
  "Write audio streams to disk using libsndfile",
  "Andy Wingo <wingo at pobox dot com>",
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_MAJOR_TYPE,
  ARG_MINOR_TYPE,
  ARG_LOOP,
  ARG_CREATE_PADS
};

static GstStaticPadTemplate sf_src_factory = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate sf_sink_factory = GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS)
    );

#define GST_TYPE_SF_MAJOR_TYPES (gst_sf_major_types_get_type())
static GType
gst_sf_major_types_get_type (void)
{
  static GType sf_major_types_type = 0;
  static GEnumValue *sf_major_types = NULL;

  if (!sf_major_types_type) {
    SF_FORMAT_INFO format_info;
    int k, count;

    sf_command (NULL, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof (int));

    sf_major_types = g_new0 (GEnumValue, count + 1);

    for (k = 0; k < count; k++) {
      format_info.format = k;
      sf_command (NULL, SFC_GET_FORMAT_MAJOR, &format_info,
	  sizeof (format_info));
      sf_major_types[k].value = format_info.format;
      sf_major_types[k].value_name = g_strdup (format_info.name);
      sf_major_types[k].value_nick = g_strdup (format_info.extension);

      /* Irritatingly enough, there exist major_types with the same extension. Let's
         just hope that sndfile gives us the list in alphabetical order, as it
         currently does. */
      if (k > 0
	  && strcmp (sf_major_types[k].value_nick,
	      sf_major_types[k - 1].value_nick) == 0) {
	g_free (sf_major_types[k].value_nick);
	sf_major_types[k].value_nick =
	    g_strconcat (sf_major_types[k - 1].value_nick, "-",
	    sf_major_types[k].value_name, NULL);
	g_strcanon (sf_major_types[k].value_nick,
	    G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
      }
    }

    sf_major_types_type =
	g_enum_register_static ("GstSndfileMajorTypes", sf_major_types);
  }
  return sf_major_types_type;
}

#define GST_TYPE_SF_MINOR_TYPES (gst_sf_minor_types_get_type())
static GType
gst_sf_minor_types_get_type (void)
{
  static GType sf_minor_types_type = 0;
  static GEnumValue *sf_minor_types = NULL;

  if (!sf_minor_types_type) {
    SF_FORMAT_INFO format_info;
    int k, count;

    sf_command (NULL, SFC_GET_FORMAT_SUBTYPE_COUNT, &count, sizeof (int));

    sf_minor_types = g_new0 (GEnumValue, count + 1);

    for (k = 0; k < count; k++) {
      format_info.format = k;
      sf_command (NULL, SFC_GET_FORMAT_SUBTYPE, &format_info,
	  sizeof (format_info));
      sf_minor_types[k].value = format_info.format;
      sf_minor_types[k].value_name = g_strdup (format_info.name);
      sf_minor_types[k].value_nick = g_ascii_strdown (format_info.name, -1);
      g_strcanon (sf_minor_types[k].value_nick, G_CSET_a_2_z G_CSET_DIGITS "-",
	  '-');
    }

    sf_minor_types_type =
	g_enum_register_static ("GstSndfileMinorTypes", sf_minor_types);
  }
  return sf_minor_types_type;
}

static void gst_sfsrc_base_init (gpointer g_class);
static void gst_sfsink_base_init (gpointer g_class);
static void gst_sf_class_init (GstSFClass * klass);
static void gst_sf_init (GstSF * this);
static void gst_sf_dispose (GObject * object);
static void gst_sf_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sf_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_sf_get_clock (GstElement * element);
static void gst_sf_set_clock (GstElement * element, GstClock * clock);
static GstPad *gst_sf_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_sf_release_request_pad (GstElement * element, GstPad * pad);
static GstElementStateReturn gst_sf_change_state (GstElement * element);

static GstPadLinkReturn gst_sf_link (GstPad * pad, const GstCaps * caps);

static void gst_sf_loop (GstElement * element);

static GstClockTime gst_sf_get_time (GstClock * clock, gpointer data);

static gboolean gst_sf_open_file (GstSF * this);
static void gst_sf_close_file (GstSF * this);

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC (gstsf_debug);
#define INFO(...) \
    GST_CAT_LEVEL_LOG (gstsf_debug, GST_LEVEL_INFO, NULL, __VA_ARGS__)
#define INFO_OBJ(obj,...) \
    GST_CAT_LEVEL_LOG (gstsf_debug, GST_LEVEL_INFO, obj, __VA_ARGS__)

GType
gst_sf_get_type (void)
{
  static GType sf_type = 0;

  if (!sf_type) {
    static const GTypeInfo sf_info = {
      sizeof (GstSFClass), NULL,
      NULL,
      (GClassInitFunc) NULL,	/* don't even initialize the class */
      NULL,
      NULL,
      sizeof (GstSF),
      0,
      (GInstanceInitFunc) NULL	/* abstract base class */
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
      sizeof (GstSFClass),
      gst_sfsrc_base_init,
      NULL,
      (GClassInitFunc) gst_sf_class_init,
      NULL,
      NULL,
      sizeof (GstSF),
      0,
      (GInstanceInitFunc) gst_sf_init,
    };
    sfsrc_type =
	g_type_register_static (GST_TYPE_SF, "GstSFSrc", &sfsrc_info, 0);
  }
  return sfsrc_type;
}

GType
gst_sfsink_get_type (void)
{
  static GType sfsink_type = 0;

  if (!sfsink_type) {
    static const GTypeInfo sfsink_info = {
      sizeof (GstSFClass),
      gst_sfsink_base_init,
      NULL,
      (GClassInitFunc) gst_sf_class_init,
      NULL,
      NULL,
      sizeof (GstSF),
      0,
      (GInstanceInitFunc) gst_sf_init,
    };
    sfsink_type =
	g_type_register_static (GST_TYPE_SF, "GstSFSink", &sfsink_info, 0);
  }
  return sfsink_type;
}

static void
gst_sfsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sf_src_factory));
  gst_element_class_set_details (element_class, &sfsrc_details);
}

static void
gst_sfsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sf_sink_factory));
  gst_element_class_set_details (element_class, &sfsink_details);
}

static void
gst_sf_class_init (GstSFClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GParamSpec *pspec;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

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
    pspec =
	g_param_spec_boolean ("create-pads", "Create pads?",
	"Create one pad for each channel in the sound file?", TRUE,
	G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property (gobject_class, ARG_CREATE_PADS, pspec);
  }

  gobject_class->dispose = gst_sf_dispose;
  gobject_class->set_property = gst_sf_set_property;
  gobject_class->get_property = gst_sf_get_property;

  gstelement_class->get_clock = gst_sf_get_clock;
  gstelement_class->set_clock = gst_sf_set_clock;
  gstelement_class->change_state = gst_sf_change_state;
  gstelement_class->request_new_pad = gst_sf_request_new_pad;
  gstelement_class->release_pad = gst_sf_release_request_pad;
}

static void
gst_sf_init (GstSF * this)
{
  gst_element_set_loop_function (GST_ELEMENT (this), gst_sf_loop);
  this->provided_clock = gst_audio_clock_new ("sfclock", gst_sf_get_time, this);
  gst_object_set_parent (GST_OBJECT (this->provided_clock), GST_OBJECT (this));
}

static void
gst_sf_dispose (GObject * object)
{
  GstSF *this = (GstSF *) object;

  gst_object_unparent (GST_OBJECT (this->provided_clock));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_sf_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
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

	for (i = g_list_length (this->channels); i < this->numchannels; i++)
	  gst_element_get_request_pad ((GstElement *) this, "src%d");
      }
      break;

    default:
      break;
  }
}

static void
gst_sf_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
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

static GstClock *
gst_sf_get_clock (GstElement * element)
{
  GstSF *this = GST_SF (element);

  return this->provided_clock;
}

static void
gst_sf_set_clock (GstElement * element, GstClock * clock)
{
  GstSF *this = GST_SF (element);

  this->clock = clock;
}

static GstClockTime
gst_sf_get_time (GstClock * clock, gpointer data)
{
  GstSF *this = GST_SF (data);

  return this->time;
}

static GstElementStateReturn
gst_sf_change_state (GstElement * element)
{
  GstSF *this = GST_SF (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (this->provided_clock), TRUE);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (this->provided_clock),
	  FALSE);
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      if (GST_FLAG_IS_SET (this, GST_SF_OPEN))
	gst_sf_close_file (this);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstPad *
gst_sf_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
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

  INFO_OBJ (element, "added pad %s\n", name);

  g_free (name);
  return channel->pad;
}

static void
gst_sf_release_request_pad (GstElement * element, GstPad * pad)
{
  GstSF *this;
  GstSFChannel *channel = NULL;
  GList *l;

  this = GST_SF (element);

  if (GST_STATE (element) == GST_STATE_PLAYING) {
    g_warning
	("You can't release a request pad if the element is PLAYING, sorry.");
    return;
  }

  for (l = this->channels; l; l = l->next) {
    if (GST_SF_CHANNEL (l)->pad == pad) {
      channel = GST_SF_CHANNEL (l);
      break;
    }
  }

  g_return_if_fail (channel != NULL);

  INFO_OBJ (element, "Releasing request pad %s", GST_PAD_NAME (channel->pad));

  if (GST_FLAG_IS_SET (element, GST_SF_OPEN))
    gst_sf_close_file (this);

  gst_element_remove_pad (element, channel->pad);
  this->channels = g_list_remove (this->channels, channel);
  this->numchannels--;
  g_free (channel);
}

static GstPadLinkReturn
gst_sf_link (GstPad * pad, const GstCaps * caps)
{
  GstSF *this = (GstSF *) GST_OBJECT_PARENT (pad);
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &this->rate);
  gst_structure_get_int (structure, "buffer-frames", &this->buffer_frames);

  INFO_OBJ (this, "linked pad %s:%s with fixed caps, rate=%d, frames=%d",
      GST_DEBUG_PAD_NAME (pad), this->rate, this->buffer_frames);

  if (this->numchannels) {
    /* we can go ahead and allocate our buffer */
    if (this->buffer)
      g_free (this->buffer);
    this->buffer =
	g_malloc (this->numchannels * this->buffer_frames * sizeof (float));
    memset (this->buffer, 0,
	this->numchannels * this->buffer_frames * sizeof (float));
  }
  return GST_PAD_LINK_OK;
}

static gboolean
gst_sf_open_file (GstSF * this)
{
  int mode;
  SF_INFO info;

  g_return_val_if_fail (!GST_FLAG_IS_SET (this, GST_SF_OPEN), FALSE);

  this->time = 0;

  if (!this->filename) {
    GST_ELEMENT_ERROR (this, RESOURCE, NOT_FOUND,
	(_("No filename specified.")), (NULL));
    return FALSE;
  }

  if (GST_IS_SFSRC (this)) {
    mode = SFM_READ;
    info.format = 0;
  } else {
    if (!this->rate) {
      INFO_OBJ (this, "Not opening %s yet because caps are not set",
	  this->filename);
      return FALSE;
    } else if (!this->numchannels) {
      INFO_OBJ (this, "Not opening %s yet because we have no input channels",
	  this->filename);
      return FALSE;
    }

    mode = SFM_WRITE;
    this->format = this->format_major | this->format_subtype;
    info.samplerate = this->rate;
    info.channels = this->numchannels;
    info.format = this->format;

    INFO_OBJ (this, "Opening %s with rate %d, %d channels, format 0x%x",
	this->filename, info.samplerate, info.channels, info.format);

    if (!sf_format_check (&info)) {
      GST_ELEMENT_ERROR (this, STREAM, ENCODE, (NULL),
	  ("Input parameters (rate:%d, channels:%d, format:0x%x) invalid",
	      info.samplerate, info.channels, info.format));
      return FALSE;
    }
  }

  this->file = sf_open (this->filename, mode, &info);

  if (!this->file) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_WRITE,
	(_("Could not open file \"%s\" for writing."), this->filename),
	("soundfile error: %s", sf_strerror (NULL)));
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

      for (i = g_list_length (this->channels); i < this->numchannels; i++)
	gst_element_get_request_pad ((GstElement *) this, "src%d");
    }

    for (l = this->channels; l; l = l->next)
      /* queue the need to set caps */
      GST_SF_CHANNEL (l)->caps_set = FALSE;
  }

  GST_FLAG_SET (this, GST_SF_OPEN);

  return TRUE;
}

static void
gst_sf_close_file (GstSF * this)
{
  int err = 0;

  g_return_if_fail (GST_FLAG_IS_SET (this, GST_SF_OPEN));

  INFO_OBJ (this, "Closing file %s", this->filename);

  if ((err = sf_close (this->file)))
    GST_ELEMENT_ERROR (this, RESOURCE, CLOSE,
	("Could not close file file \"%s\".", this->filename),
	("soundfile error: %s", strerror (err)));
  else
    GST_FLAG_UNSET (this, GST_SF_OPEN);

  this->file = NULL;
  if (this->buffer)
    g_free (this->buffer);
  this->buffer = NULL;
}

static void
gst_sf_loop (GstElement * element)
{
  GstSF *this;
  GList *l = NULL;

  this = (GstSF *) element;

  if (this->channels == NULL) {
    GST_ELEMENT_ERROR (element, CORE, PAD, (NULL),
	("You must connect at least one pad to sndfile elements."));
    return;
  }

  if (GST_IS_SFSRC (this)) {
    sf_count_t read;
    gint i, j;
    int eos = 0;
    int buffer_frames = this->buffer_frames;
    int nchannels = this->numchannels;
    GstSFChannel *channel = NULL;
    gfloat *data;
    gfloat *buf = this->buffer;
    GstBuffer *out;

    if (!GST_FLAG_IS_SET (this, GST_SF_OPEN))
      if (!gst_sf_open_file (this))
	return;			/* we've already set gst_element_error */

    if (buffer_frames == 0) {
      /* we have to set the caps later */
      buffer_frames = this->buffer_frames = 1024;
    }
    if (buf == NULL) {
      buf = this->buffer =
	  g_malloc (this->numchannels * this->buffer_frames * sizeof (float));
      memset (this->buffer, 0,
	  this->numchannels * this->buffer_frames * sizeof (float));
    }

    read = sf_readf_float (this->file, buf, buffer_frames);
    if (read < buffer_frames)
      eos = 1;

    if (read)
      for (i = 0, l = this->channels; l; l = l->next, i++) {
	channel = GST_SF_CHANNEL (l);

	/* don't push on disconnected pads -- useful for ::create-pads=TRUE */
	if (!GST_PAD_PEER (channel->pad))
	  continue;

	if (!channel->caps_set) {
	  GstCaps *caps =
	      gst_caps_copy (GST_PAD_CAPS (GST_SF_CHANNEL (l)->pad));
	  if (!caps)
	    caps = gst_caps_copy
		(GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (GST_SF_CHANNEL
			(l)->pad)));
	  gst_caps_set_simple (caps, "rate", G_TYPE_INT, this->rate,
	      "buffer-frames", G_TYPE_INT, this->buffer_frames, NULL);
	  if (!gst_pad_try_set_caps (GST_SF_CHANNEL (l)->pad, caps)) {
	    GST_ELEMENT_ERROR (this, CORE, NEGOTIATION, (NULL),
		("Opened file with sample rate %d, but could not set caps",
		    this->rate));
	    gst_sf_close_file (this);
	    return;
	  }
	  channel->caps_set = TRUE;
	}

	out = gst_buffer_new_and_alloc (read * sizeof (float));
	data = (gfloat *) GST_BUFFER_DATA (out);
	for (j = 0; j < read; j++)
	  data[j] = buf[j * nchannels + i % nchannels];
	gst_pad_push (channel->pad, GST_DATA (out));
      }

    this->time += read * (GST_SECOND / this->rate);
    gst_audio_clock_update_time ((GstAudioClock *) this->provided_clock,
	this->time);

    if (eos) {
      if (this->loop) {
	sf_seek (this->file, (sf_count_t) 0, SEEK_SET);
	eos = 0;
      } else {
	for (l = this->channels; l; l = l->next)
	  gst_pad_push (GST_SF_CHANNEL (l)->pad,
	      GST_DATA (gst_event_new (GST_EVENT_EOS)));
	gst_element_set_eos (element);
      }
    }
  } else {
    sf_count_t written, num_to_write;
    gint i, j;
    int buffer_frames = this->buffer_frames;
    int nchannels = this->numchannels;
    GstSFChannel *channel = NULL;
    gfloat *data;
    gfloat *buf = this->buffer;
    GstBuffer *in;

    /* the problem: we can't allocate a buffer for pulled data before caps is
     * set, and we can't open the file without the sample rate from the
     * caps... */

    num_to_write = buffer_frames;

    INFO_OBJ (this, "looping, buffer_frames=%d, nchannels=%d", buffer_frames,
	nchannels);

    for (i = 0, l = this->channels; l; l = l->next, i++) {
      channel = GST_SF_CHANNEL (l);

    pull_again:
      in = GST_BUFFER (gst_pad_pull (channel->pad));

      if (buffer_frames == 0) {
	/* pulling a buffer from the pad should have caused capsnego to occur,
	   which then would set this->buffer_frames to a new value */
	buffer_frames = this->buffer_frames;
	if (buffer_frames == 0) {
	  GST_ELEMENT_ERROR (element, CORE, NEGOTIATION, (NULL),
	      ("format wasn't negotiated before chain function"));
	  return;
	}
	buf = this->buffer;
	num_to_write = buffer_frames;
      }

      if (!GST_FLAG_IS_SET (this, GST_SF_OPEN))
	if (!gst_sf_open_file (this))
	  return;		/* we've already set gst_element_error */

      if (GST_IS_EVENT (in)) {
	switch (GST_EVENT_TYPE (in)) {
	  case GST_EVENT_EOS:
	  case GST_EVENT_INTERRUPT:
	    num_to_write = 0;
	    break;
	  default:
	    goto pull_again;
	    break;
	}
      }

      if (num_to_write) {
	data = (gfloat *) GST_BUFFER_DATA (in);
	num_to_write =
	    MIN (num_to_write, GST_BUFFER_SIZE (in) / sizeof (gfloat));
	for (j = 0; j < num_to_write; j++)
	  buf[j * nchannels + i % nchannels] = data[j];
      }

      gst_data_unref ((GstData *) in);
    }

    if (num_to_write) {
      written = sf_writef_float (this->file, buf, num_to_write);
      if (written != num_to_write)
	GST_ELEMENT_ERROR (element, RESOURCE, WRITE,
	    (_("Could not write to file \"%s\"."), this->filename),
	    ("soundfile error: %s", sf_strerror (this->file)));
    }

    this->time += num_to_write * (GST_SECOND / this->rate);
    gst_audio_clock_update_time ((GstAudioClock *) this->provided_clock,
	this->time);

    if (num_to_write != buffer_frames)
      gst_element_set_eos (element);
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstaudio"))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gstsf_debug, "sf",
      GST_DEBUG_FG_WHITE | GST_DEBUG_BG_GREEN | GST_DEBUG_BOLD,
      "libsndfile plugin");

  if (!gst_element_register (plugin, "sfsrc", GST_RANK_NONE, GST_TYPE_SFSRC))
    return FALSE;

  if (!gst_element_register (plugin, "sfsink", GST_RANK_NONE, GST_TYPE_SFSINK))
    return FALSE;

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstsf",
    "Sndfile plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
