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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vorbis/vorbisenc.h>

#include <gst/gsttaginterface.h>
#include "vorbisenc.h"

static GstPadTemplate *gst_vorbisenc_src_template, *gst_vorbisenc_sink_template;

/* elementfactory information */
GstElementDetails vorbisenc_details = {
  "Ogg Vorbis encoder",
  "Codec/Encoder/Audio",
  "Encodes audio in OGG Vorbis format",
  "Monty <monty@xiph.org>, "
  "Wim Taymans <wim.taymans@chello.be>",
};

/* VorbisEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MAX_BITRATE,
  ARG_BITRATE,
  ARG_MIN_BITRATE,
  ARG_QUALITY,
  ARG_SERIAL,
  ARG_MANAGED,
  ARG_LAST_MESSAGE,
};

static const GstFormat*
gst_vorbisenc_get_formats (GstPad *pad)
{
  static const GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };
  static const GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_TIME, 
    0
  };
	      
  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
} 

#define MAX_BITRATE_DEFAULT 	-1
#define BITRATE_DEFAULT 	-1
#define MIN_BITRATE_DEFAULT 	-1
#define QUALITY_DEFAULT 	0.3

static void             gst_vorbisenc_base_init         (gpointer g_class);
static void 		gst_vorbisenc_class_init 	(VorbisEncClass *klass);
static void 		gst_vorbisenc_init 		(VorbisEnc *vorbisenc);

static void 		gst_vorbisenc_chain 		(GstPad *pad, GstData *_data);
static gboolean 	gst_vorbisenc_setup 		(VorbisEnc *vorbisenc);

static void 		gst_vorbisenc_get_property 	(GObject *object, guint prop_id, 
		        	                         GValue *value, GParamSpec *pspec);
static void 		gst_vorbisenc_set_property 	(GObject *object, guint prop_id, 
			                                 const GValue *value, GParamSpec *pspec);
static GstElementStateReturn
			gst_vorbisenc_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_vorbisenc_signals[LAST_SIGNAL] = { 0 }; */

GType
vorbisenc_get_type (void)
{
  static GType vorbisenc_type = 0;

  if (!vorbisenc_type) {
    static const GTypeInfo vorbisenc_info = {
      sizeof (VorbisEncClass), 
      gst_vorbisenc_base_init,
      NULL,
      (GClassInitFunc) gst_vorbisenc_class_init,
      NULL,
      NULL,
      sizeof (VorbisEnc),
      0,
      (GInstanceInitFunc) gst_vorbisenc_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };
    
    vorbisenc_type = g_type_register_static (GST_TYPE_ELEMENT, "VorbisEnc", &vorbisenc_info, 0);
    
    g_type_add_interface_static (vorbisenc_type, GST_TYPE_TAG_SETTER, &tag_setter_info);
  }
  return vorbisenc_type;
}

static GstCaps*
vorbis_caps_factory (void)
{
  return gst_caps_new_simple ("application/ogg", NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   gst_caps_new_simple ("audio/x-raw-int",
       "endianness", 	G_TYPE_INT, G_BYTE_ORDER,
       "signed", 	G_TYPE_BOOLEAN, TRUE,
       "width", 	G_TYPE_INT, 16,
       "depth",    	G_TYPE_INT, 16,
       "rate",     	GST_TYPE_INT_RANGE, 11025, 48000,
       "channels", 	GST_TYPE_INT_RANGE, 1, 2,
       NULL);
}

static void
gst_vorbisenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *vorbis_caps;

  raw_caps = raw_caps_factory ();
  vorbis_caps = vorbis_caps_factory ();

  gst_vorbisenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                                      GST_PAD_ALWAYS, 
					              raw_caps);
  gst_vorbisenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                                     GST_PAD_ALWAYS, 
					             vorbis_caps);
  gst_element_class_add_pad_template (element_class, gst_vorbisenc_sink_template);
  gst_element_class_add_pad_template (element_class, gst_vorbisenc_src_template);
  gst_element_class_set_details (element_class, &vorbisenc_details);
}

static void
gst_vorbisenc_class_init (VorbisEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_BITRATE, 
    g_param_spec_int ("max_bitrate", "Max bitrate", 
	    " Specify a minimum bitrate (in bps). Useful for encoding for a fixed-size channel", 
	    -1, G_MAXINT, MAX_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE, 
    g_param_spec_int ("bitrate", "Bitrate", "Choose a bitrate to encode at. "
	    "Attempt to encode at a bitrate averaging this. Takes an argument in kbps.", 
	    -1, G_MAXINT, BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MIN_BITRATE, 
    g_param_spec_int ("min_bitrate", "Min bitrate", 
	    "Specify a maximum bitrate in bps. Useful for streaming applications.",
	    -1, G_MAXINT, MIN_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY, 
    g_param_spec_float ("quality", "Quality", 
	    "Specify quality instead of specifying a particular bitrate.",
	    0.0, 1.0, QUALITY_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SERIAL, 
    g_param_spec_int ("serial", "Serial", "Specify a serial number for the stream. (-1 is random)",
	    -1, G_MAXINT, -1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MANAGED, 
    g_param_spec_boolean ("managed", "Managed", "Enable bitrate management engine",
	    FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
    g_param_spec_string ("last-message", "last-message", "The last status message",
            NULL, G_PARAM_READABLE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_vorbisenc_set_property;
  gobject_class->get_property = gst_vorbisenc_get_property;

  gstelement_class->change_state = gst_vorbisenc_change_state;
}

static GstPadLinkReturn
gst_vorbisenc_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  VorbisEnc *vorbisenc;
  GstStructure *structure;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int  (structure, "channels", &vorbisenc->channels);
  gst_structure_get_int  (structure, "rate",     &vorbisenc->frequency);

  gst_vorbisenc_setup (vorbisenc);

  if (vorbisenc->setup)
    return GST_PAD_LINK_OK;

  return GST_PAD_LINK_REFUSED;
}

static gboolean
gst_vorbisenc_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value, 
		      GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  VorbisEnc *vorbisenc;
  gint64 avg;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  if (vorbisenc->samples_in == 0 || 
      vorbisenc->bytes_out == 0 || 
      vorbisenc->frequency == 0)
    return FALSE;

  avg = (vorbisenc->bytes_out * vorbisenc->frequency)/
		  (vorbisenc->samples_in);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / avg;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * avg / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static gboolean
gst_vorbisenc_convert_sink (GstPad *pad, GstFormat src_format, gint64 src_value, 
		     GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));
  
  bytes_per_sample = vorbisenc->channels * 2;
  
  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
	  if (bytes_per_sample == 0)
            return FALSE;
	  *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
	{
          gint byterate = bytes_per_sample * vorbisenc->frequency;

	  if (byterate == 0)
            return FALSE;
	  *dest_value = src_value * GST_SECOND / byterate;
          break;
	}
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * bytes_per_sample;
	  break;
        case GST_FORMAT_TIME:
	  if (vorbisenc->frequency == 0)
            return FALSE;
	  *dest_value = src_value * GST_SECOND / vorbisenc->frequency;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  scale = bytes_per_sample;
	  /* fallthrough */
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value * scale * vorbisenc->frequency / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static const GstQueryType*
gst_vorbisenc_get_query_types (GstPad *pad)
{
  static const GstQueryType gst_vorbisenc_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return gst_vorbisenc_src_query_types;
}

static gboolean
gst_vorbisenc_src_query (GstPad *pad, GstQueryType type,
		   GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  VorbisEnc *vorbisenc;
 
  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
	case GST_FORMAT_BYTES:
	case GST_FORMAT_TIME:
        {
	  gint64 peer_value;
          const GstFormat *peer_formats;

	  res = FALSE;

	  peer_formats = gst_pad_get_formats (GST_PAD_PEER (vorbisenc->sinkpad));

	  while (peer_formats && *peer_formats && !res) {

	    GstFormat peer_format = *peer_formats;

	    /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (vorbisenc->sinkpad), GST_QUERY_TOTAL,
			       &peer_format, &peer_value)) 
	    {
              GstFormat conv_format;
	      /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
	      res = gst_pad_convert (vorbisenc->sinkpad,
				peer_format, peer_value,
				&conv_format, value);
	      /* and to final format */
	      res &= gst_pad_convert (pad,
			GST_FORMAT_TIME, *value,
			format, value);
	    }
	    peer_formats++;
	  }
	  break;
	}
	default:
	  res = FALSE;
	  break;
      }
      break;
    }
    case GST_QUERY_POSITION:
      switch (*format) {
	default:
	{
	  /* we only know about our samples, convert to requested format */
	  res = gst_pad_convert (pad,
			  GST_FORMAT_BYTES, vorbisenc->bytes_out,
			  format, value);
	  break;
	}
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static void
gst_vorbisenc_init (VorbisEnc * vorbisenc)
{
  vorbisenc->sinkpad = gst_pad_new_from_template (gst_vorbisenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->sinkpad);
  gst_pad_set_chain_function (vorbisenc->sinkpad, gst_vorbisenc_chain);
  gst_pad_set_link_function (vorbisenc->sinkpad, gst_vorbisenc_sinkconnect);
  gst_pad_set_convert_function (vorbisenc->sinkpad, GST_DEBUG_FUNCPTR (gst_vorbisenc_convert_sink));
  gst_pad_set_formats_function (vorbisenc->sinkpad, GST_DEBUG_FUNCPTR (gst_vorbisenc_get_formats));

  vorbisenc->srcpad = gst_pad_new_from_template (gst_vorbisenc_src_template, "src");
  gst_pad_set_query_function (vorbisenc->srcpad, GST_DEBUG_FUNCPTR (gst_vorbisenc_src_query));
  gst_pad_set_query_type_function (vorbisenc->srcpad, GST_DEBUG_FUNCPTR (gst_vorbisenc_get_query_types));
  gst_pad_set_convert_function (vorbisenc->srcpad, GST_DEBUG_FUNCPTR (gst_vorbisenc_convert_src));
  gst_pad_set_formats_function (vorbisenc->srcpad, GST_DEBUG_FUNCPTR (gst_vorbisenc_get_formats));
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->srcpad);

  vorbisenc->channels = -1;
  vorbisenc->frequency = -1;

  vorbisenc->managed = FALSE;
  vorbisenc->max_bitrate = MAX_BITRATE_DEFAULT;
  vorbisenc->bitrate = BITRATE_DEFAULT;
  vorbisenc->min_bitrate = MIN_BITRATE_DEFAULT;
  vorbisenc->quality = QUALITY_DEFAULT;
  vorbisenc->quality_set = FALSE;
  vorbisenc->serial = -1;
  vorbisenc->last_message = NULL;
  
  vorbisenc->setup = FALSE;
  vorbisenc->eos = FALSE;
  vorbisenc->header_sent = FALSE;

  vorbisenc->tags = gst_tag_list_new ();
  
  /* we're chained and we can deal with events */
  GST_FLAG_SET (vorbisenc, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_vorbisenc_metadata_set1 (const GstTagList *list, const gchar *tag, gpointer vorbisenc)
{
  gchar *vorbistag = NULL;
  gchar *vorbisvalue = NULL;
  guint i, count;
  VorbisEnc *enc = GST_VORBISENC (vorbisenc);

  count = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < count; i++) {
    /* get tag name right */
    if (strcmp (tag, GST_TAG_TITLE) == 0) {
      vorbistag = g_strdup ("TITLE");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_VERSION) == 0) {
      vorbistag = g_strdup ("VERSION");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_ALBUM) == 0) {
      vorbistag = g_strdup ("ALBUM");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_TRACK_NUMBER) == 0) {
      guint track_no;
      vorbistag = g_strdup ("TRACKNUMBER");
      g_assert (gst_tag_list_get_uint_index (list, tag, i, &track_no));
      vorbisvalue = g_strdup_printf ("%u", track_no);
    } else if (strcmp (tag, GST_TAG_ARTIST) == 0) {
      vorbistag = g_strdup ("ARTIST");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_PERFORMER) == 0) {
      vorbistag = g_strdup ("PERFORMER");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_COPYRIGHT) == 0) {
      vorbistag = g_strdup ("COPYRIGHT");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_LICENSE) == 0) {
      vorbistag = g_strdup ("LICENSE");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_ORGANIZATION) == 0) {
      vorbistag = g_strdup ("ORGANIZATION");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_DESCRIPTION) == 0) {
      vorbistag = g_strdup ("DESCRIPTION");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_GENRE) == 0) {
      vorbistag = g_strdup ("GENRE");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_DATE) == 0) {
      /* FIXME: how are dates represented in vorbis files? */
      GDate *date;
      guint u;
      
      vorbistag = g_strdup ("DATE");
      g_assert (gst_tag_list_get_uint_index (list, tag, i, &u));
      date = g_date_new_julian (u);
      vorbisvalue = g_strdup_printf ("%04d-%02d-%02d", (gint) g_date_get_year (date),
				   (gint) g_date_get_month (date), (gint) g_date_get_day (date));
      g_date_free (date);
    /* NOTE: GST_TAG_LOCATION != vorbis' location
    } else if (strcmp (tag, PLACE) == 0) {
      vorbistag = g_strdup ("LOCATION");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    */
    } else if (strcmp (tag, GST_TAG_CONTACT) == 0) {
      vorbistag = g_strdup ("CONTACT");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else if (strcmp (tag, GST_TAG_ISRC) == 0) {
      vorbistag = g_strdup ("ISRC");
      g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
    } else {
      vorbistag = g_ascii_strup (tag, -1);
      if (gst_tag_get_type (tag) == G_TYPE_STRING) {
	g_assert (gst_tag_list_get_string_index (list, tag, i, &vorbisvalue));
      } else {
	const GValue *value = gst_tag_list_get_value_index (list, tag, i);
        vorbisvalue = g_strdup_value_contents (value);
      }
    }
  }

  vorbis_comment_add_tag (&enc->vc, vorbistag, vorbisvalue);
}
static void 
gst_vorbisenc_set_metadata (VorbisEnc *vorbisenc)
{
  GstTagList *copy; 
  const GstTagList *user_tags;
  
  user_tags = gst_tag_setter_get_list (GST_TAG_SETTER (vorbisenc));
  if (!(vorbisenc->tags || user_tags))
    return;

  copy = gst_tag_list_merge (user_tags, vorbisenc->tags, gst_tag_setter_get_merge_mode (GST_TAG_SETTER (vorbisenc)));
  vorbis_comment_init (&vorbisenc->vc);
  gst_tag_list_foreach (copy, gst_vorbisenc_metadata_set1, vorbisenc);
  gst_tag_list_free (copy);
}

static gchar*
get_constraints_string (VorbisEnc *vorbisenc)
{
  gint min = vorbisenc->min_bitrate;
  gint max = vorbisenc->max_bitrate;
  gchar *result;

  if (min > 0 && max > 0)
    result = g_strdup_printf ("(min %d bps, max %d bps)", min,max);
  else if (min > 0)
    result = g_strdup_printf ("(min %d bps, no max)", min);
  else if (max > 0)
    result = g_strdup_printf ("(no min, max %d bps)", max);
  else
    result = g_strdup_printf ("(no min or max)");

  return result;
}

static void
update_start_message (VorbisEnc *vorbisenc) 
{
  gchar *constraints;

  g_free (vorbisenc->last_message);

  if (vorbisenc->bitrate > 0) {
    if (vorbisenc->managed) {
      constraints = get_constraints_string (vorbisenc);
      vorbisenc->last_message = 
	      g_strdup_printf ("encoding at average bitrate %d bps %s", 
			       vorbisenc->bitrate, constraints);
      g_free (constraints);
    }
    else {
      vorbisenc->last_message = 
	      g_strdup_printf ("encoding at approximate bitrate %d bps (VBR encoding enabled)", 
			       vorbisenc->bitrate);
    }
  }
  else {
    if (vorbisenc->quality_set) {
      if (vorbisenc->managed) {
        constraints = get_constraints_string (vorbisenc);
        vorbisenc->last_message = 
	      g_strdup_printf ("encoding at quality level %2.2f using constrained VBR %s", 
			       vorbisenc->quality, constraints);
        g_free (constraints);
      }
      else {
        vorbisenc->last_message = 
	      g_strdup_printf ("encoding at quality level %2.2f", 
			       vorbisenc->quality);
      }
    }
    else {
      constraints = get_constraints_string (vorbisenc);
      vorbisenc->last_message = 
	      g_strdup_printf ("encoding using bitrate management %s", 
			       constraints);
      g_free (constraints);
    }
  }

  g_object_notify (G_OBJECT (vorbisenc), "last_message");
}

static gboolean
gst_vorbisenc_setup (VorbisEnc *vorbisenc)
{
  gint serial;

  if (vorbisenc->bitrate < 0 && vorbisenc->min_bitrate < 0 && vorbisenc->max_bitrate < 0) {
    vorbisenc->quality_set = TRUE;
  }

  update_start_message (vorbisenc);
  
  /* choose an encoding mode */
  /* (mode 0: 44kHz stereo uncoupled, roughly 128kbps VBR) */
  vorbis_info_init (&vorbisenc->vi);

  if(vorbisenc->quality_set){
    if (vorbis_encode_setup_vbr (&vorbisenc->vi, 
			         vorbisenc->channels, 
				 vorbisenc->frequency, 
				 vorbisenc->quality)) 
    {
       g_warning ("vorbisenc: initialisation failed: invalid parameters for quality");
       vorbis_info_clear(&vorbisenc->vi);
       return FALSE;
    }

    /* do we have optional hard quality restrictions? */
    if(vorbisenc->max_bitrate > 0 || vorbisenc->min_bitrate > 0){
      struct ovectl_ratemanage_arg ai;
      vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_GET, &ai);

      /* the bitrates are in kHz */
      ai.bitrate_hard_min = vorbisenc->min_bitrate / 1000;
      ai.bitrate_hard_max = vorbisenc->max_bitrate / 1000;
      ai.management_active = 1;

      vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_SET, &ai);
    }
  } 
  else {
    if (vorbis_encode_setup_managed (&vorbisenc->vi, 
			             vorbisenc->channels, 
				     vorbisenc->frequency,
                                     vorbisenc->max_bitrate > 0 ? vorbisenc->max_bitrate : -1,
                                     vorbisenc->bitrate,
                                     vorbisenc->min_bitrate > 0 ? vorbisenc->min_bitrate : -1))
    {
      g_warning("vorbisenc: initialisation failed: invalid parameters for bitrate\n");
      vorbis_info_clear(&vorbisenc->vi);
      return FALSE;
    }
  }

  if(vorbisenc->managed && vorbisenc->bitrate < 0) {
    vorbis_encode_ctl(&vorbisenc->vi, OV_ECTL_RATEMANAGE_AVG, NULL);
  }
  else if(!vorbisenc->managed) {
    /* Turn off management entirely (if it was turned on). */
    vorbis_encode_ctl(&vorbisenc->vi, OV_ECTL_RATEMANAGE_SET, NULL);
  }
  vorbis_encode_setup_init(&vorbisenc->vi);

  /* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init (&vorbisenc->vd, &vorbisenc->vi);
  vorbis_block_init (&vorbisenc->vd, &vorbisenc->vb);

  /* set up our packet->stream encoder */
  /* pick a random serial number; that way we can more likely build
     chained streams just by concatenation */
  if (vorbisenc->serial < 0) {
    srand (time (NULL));
    serial = rand ();
  }
  else {
    serial = vorbisenc->serial;
  }

  ogg_stream_init (&vorbisenc->os, serial);

  vorbisenc->setup = TRUE;

  return TRUE;
}

static void
gst_vorbisenc_write_page (VorbisEnc *vorbisenc, ogg_page *page)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (page->header_len + 
			             page->body_len);

  memcpy (GST_BUFFER_DATA (outbuf), page->header, 
				    page->header_len);
  memcpy (GST_BUFFER_DATA (outbuf) + page->header_len, 
			             page->body,
	        		     page->body_len);

  GST_DEBUG ("vorbisenc: encoded buffer of %d bytes", 
			GST_BUFFER_SIZE (outbuf));

  vorbisenc->bytes_out += GST_BUFFER_SIZE (outbuf);

  if (GST_PAD_IS_USABLE (vorbisenc->srcpad)) {
    gst_pad_push (vorbisenc->srcpad, GST_DATA (outbuf));
  }
  else {
    gst_buffer_unref (outbuf);
  }
}

static void
gst_vorbisenc_chain (GstPad * pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  VorbisEnc *vorbisenc;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        /* end of file.  this can be done implicitly in the mainline,
           but it's easier to see here in non-clever fashion.
           Tell the library we're at end of stream so that it can handle
           the last frame and mark end of stream in the output properly */
        vorbis_analysis_wrote (&vorbisenc->vd, 0);
	gst_event_unref (event);
	break;
      case GST_EVENT_TAG:
	if (vorbisenc->tags) {
	  gst_tag_list_insert (vorbisenc->tags, gst_event_tag_get_list (event), 
		  gst_tag_setter_get_merge_mode (GST_TAG_SETTER (vorbisenc)));
	} else {
	  g_assert_not_reached ();
	}
	gst_pad_event_default (pad, event);
	return;
      default:
	gst_pad_event_default (pad, event);
        return;
    }
  }
  else {
    gint16 *data;
    gulong size;
    gulong i, j;
    float **buffer;

    if (!vorbisenc->setup) {
      gst_buffer_unref (buf);
      GST_ELEMENT_ERROR (vorbisenc, CORE, NEGOTIATION, (NULL), ("encoder not initialized (input is not audio?)"));
      return;
    }

    if (!vorbisenc->header_sent) {
      gint result;
      /* Vorbis streams begin with three headers; the initial header (with
	 most of the codec setup parameters) which is mandated by the Ogg
	 bitstream spec.  The second header holds any comment fields.  The
	 third header holds the bitstream codebook.  We merely need to
	 make the headers, then pass them to libvorbis one at a time;
	 libvorbis handles the additional Ogg bitstream constraints */
      ogg_packet header;
      ogg_packet header_comm;
      ogg_packet header_code;

      gst_vorbisenc_set_metadata (vorbisenc);
      vorbis_analysis_headerout (&vorbisenc->vd, &vorbisenc->vc, &header, &header_comm, &header_code);
      ogg_stream_packetin (&vorbisenc->os, &header); /* automatically placed in its own page */
      ogg_stream_packetin (&vorbisenc->os, &header_comm);
      ogg_stream_packetin (&vorbisenc->os, &header_code);

      while ((result = ogg_stream_flush(&vorbisenc->os, &vorbisenc->og))) {
	gst_vorbisenc_write_page (vorbisenc, &vorbisenc->og);
      }
      vorbisenc->header_sent = TRUE;
    }
  
    /* data to encode */
    data = (gint16 *) GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf) / (vorbisenc->channels * 2);

    /* expose the buffer to submit data */
    buffer = vorbis_analysis_buffer (&vorbisenc->vd, size);

    /* uninterleave samples */
    for (i = 0; i < size; i++) {
      for (j = 0; j < vorbisenc->channels; j++) {
	buffer[j][i] = data[i * vorbisenc->channels + j] / 32768.f;
      }
    }

    /* tell the library how much we actually submitted */
    vorbis_analysis_wrote (&vorbisenc->vd, size);

    vorbisenc->samples_in += size;

    gst_buffer_unref (buf);
  }

  /* vorbis does some data preanalysis, then divvies up blocks for
     more involved (potentially parallel) processing.  Get a single
     block for encoding now */
  while (vorbis_analysis_blockout (&vorbisenc->vd, &vorbisenc->vb) == 1) {

    /* analysis */
    vorbis_analysis (&vorbisenc->vb, NULL);
    vorbis_bitrate_addblock(&vorbisenc->vb);
    
    while (vorbis_bitrate_flushpacket (&vorbisenc->vd, &vorbisenc->op)) {

      /* weld the packet into the bitstream */
      ogg_stream_packetin (&vorbisenc->os, &vorbisenc->op);

      /* write out pages (if any) */
      while (!vorbisenc->eos) {
        int result = ogg_stream_pageout (&vorbisenc->os, &vorbisenc->og);

        if (result == 0)
	  break;

	gst_vorbisenc_write_page (vorbisenc, &vorbisenc->og);

        /* this could be set above, but for illustrative purposes, I do
           it here (to show that vorbis does know where the stream ends) */
        if (ogg_page_eos (&vorbisenc->og)) {
	  vorbisenc->eos = 1;
        }
      }
    }
  }

  if (vorbisenc->eos) {
    /* clean up and exit.  vorbis_info_clear() must be called last */
    ogg_stream_clear (&vorbisenc->os);
    vorbis_block_clear (&vorbisenc->vb);
    vorbis_dsp_clear (&vorbisenc->vd);
    vorbis_info_clear (&vorbisenc->vi);
    gst_pad_push (vorbisenc->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (vorbisenc));
  }
}

static void
gst_vorbisenc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  VorbisEnc *vorbisenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_MAX_BITRATE:
      g_value_set_int (value, vorbisenc->max_bitrate);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, vorbisenc->bitrate);
      break;
    case ARG_MIN_BITRATE:
      g_value_set_int (value, vorbisenc->min_bitrate);
      break;
    case ARG_QUALITY:
      g_value_set_float (value, vorbisenc->quality);
      break;
    case ARG_SERIAL:
      g_value_set_int (value, vorbisenc->serial);
      break;
    case ARG_MANAGED:
      g_value_set_boolean (value, vorbisenc->managed);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, vorbisenc->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vorbisenc_set_property (GObject * object, guint prop_id, const GValue * value,
			    GParamSpec * pspec)
{
  VorbisEnc *vorbisenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_MAX_BITRATE:
    {
      gboolean old_value = vorbisenc->managed;

      vorbisenc->max_bitrate = g_value_get_int (value);
      if (vorbisenc->min_bitrate > 0 && vorbisenc->max_bitrate > 0)
        vorbisenc->managed = TRUE;
      else
        vorbisenc->managed = FALSE;

      if (old_value != vorbisenc->managed)
	g_object_notify (object, "managed");
      break;
    }
    case ARG_BITRATE:
      vorbisenc->bitrate = g_value_get_int (value);
      break;
    case ARG_MIN_BITRATE:
    {
      gboolean old_value = vorbisenc->managed;

      vorbisenc->min_bitrate = g_value_get_int (value);
      if (vorbisenc->min_bitrate > 0 && vorbisenc->max_bitrate > 0)
        vorbisenc->managed = TRUE;
      else
        vorbisenc->managed = FALSE;

      if (old_value != vorbisenc->managed)
	g_object_notify (object, "managed");
      break;
    }
    case ARG_QUALITY:
      vorbisenc->quality = g_value_get_float (value);
      if (vorbisenc->quality >= 0.0)
        vorbisenc->quality_set = TRUE;
      else
        vorbisenc->quality_set = FALSE;
      break;
    case ARG_SERIAL:
      vorbisenc->serial = g_value_get_int (value);
      break;
    case ARG_MANAGED:
      vorbisenc->managed = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_vorbisenc_change_state (GstElement *element)
{
  VorbisEnc *vorbisenc = GST_VORBISENC (element);
    
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      vorbisenc->eos = FALSE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      vorbisenc->setup = FALSE;
      vorbisenc->header_sent = FALSE;
      gst_tag_list_free (vorbisenc->tags);
      vorbisenc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

