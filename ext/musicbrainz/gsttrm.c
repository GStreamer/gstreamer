/* GStreamer trm plugin
 * Copyright (C) 2004 Jeremy Simon <jsimon13@yahoo.fr>
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
#include <sys/time.h>

#include "gsttrm.h"

/* musicbrainz signals and args */
enum {
  SIGNAL_SIGNATURE_AVAILABLE,
  LAST_SIGNAL
};


enum {
  ARG_0,
  ARG_SIGNATURE,
  ARG_ASCII_SIGNATURE
};


GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (bool) TRUE, "
      "width = (int) { 8, 16 }, "
      "depth = (int) { 8, 16 }, "
      "rate = (int) [ 8000, 96000 ], "
      "channels = (int) [ 1, 2 ]"
  )
);


GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (bool) TRUE, "
      "width = (int) { 8, 16 }, "
      "depth = (int) { 8, 16 }, "
      "rate = (int) [ 8000, 96000 ], "
      "channels = (int) [ 1, 2 ]"
  )
);

   
static void	gst_musicbrainz_class_init		(GstMusicBrainzClass *klass);
static void	gst_musicbrainz_base_init 		(GstMusicBrainzClass *klass);
static void	gst_musicbrainz_init	 		(GstMusicBrainz *musicbrainz);

static void	gst_musicbrainz_chain			(GstPad *pad, GstData *data);

static void	gst_musicbrainz_set_property 		(GObject *object, guint prop_id, 
					  		 const GValue *value, GParamSpec *pspec);
static void	gst_musicbrainz_get_property		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstElementStateReturn 
		gst_musicbrainz_change_state 		(GstElement *element);


static GstElementClass *parent_class = NULL;
static guint gst_musicbrainz_signals[LAST_SIGNAL] = { 0 };


GType
gst_musicbrainz_get_type (void)
{
  static GType musicbrainz_type = 0;

  if (!musicbrainz_type) {
    static const GTypeInfo musicbrainz_info = {
      sizeof(GstMusicBrainzClass),
      (GBaseInitFunc) gst_musicbrainz_base_init,
      NULL,
      (GClassInitFunc) gst_musicbrainz_class_init,
      NULL,
      NULL,
      sizeof(GstMusicBrainz),
      0,
      (GInstanceInitFunc)gst_musicbrainz_init,
    };
    musicbrainz_type = g_type_register_static (GST_TYPE_ELEMENT, 
						"GstMusicBrainz", 
						&musicbrainz_info, 0);
  }
  return musicbrainz_type;
}


static void
gst_musicbrainz_base_init (GstMusicBrainzClass *klass)
{
  GstElementDetails gst_musicbrainz_details = {
    "Compute TRM Id",
    "Codec/Audio/Decoder",
    "Computr TRM Id from muscibrainz",
    "Jeremy Simon <jsimon13@yahoo.fr>",
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_musicbrainz_details);
}



static void
gst_musicbrainz_class_init (GstMusicBrainzClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SIGNATURE,
    g_param_spec_string ("signature","signature","signature", 
                         NULL, G_PARAM_READABLE)); 
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ASCII_SIGNATURE,
    g_param_spec_string ("ascii_signature","ascii_signature","ascii_signature",
                         NULL, G_PARAM_READABLE));

  gobject_class->set_property = gst_musicbrainz_set_property;
  gobject_class->get_property = gst_musicbrainz_get_property;

  gst_musicbrainz_signals[SIGNAL_SIGNATURE_AVAILABLE] =
    g_signal_new ("signture_available", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstMusicBrainzClass, signature_available), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gstelement_class->change_state = gst_musicbrainz_change_state;
}

static GstPadLinkReturn
gst_musicbrainz_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstMusicBrainz *musicbrainz;
  GstStructure *structure;
  const gchar *mimetype;
  gint width;

  musicbrainz = GST_MUSICBRAINZ (gst_pad_get_parent (pad)); 
 
  musicbrainz->caps = caps;

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  if (!gst_structure_get_int (structure, "depth", &musicbrainz->depth) ||
      !gst_structure_get_int (structure, "width", &width))
      return GST_PAD_LINK_REFUSED;

  if (musicbrainz->depth != width)
      return GST_PAD_LINK_REFUSED;

  if (!gst_structure_get_int (structure, "channels", &musicbrainz->channels))
      return GST_PAD_LINK_REFUSED;

  if (!gst_structure_get_int (structure, "rate", &musicbrainz->rate))
      return GST_PAD_LINK_REFUSED;

  trm_SetPCMDataInfo (musicbrainz->trm, musicbrainz->rate, musicbrainz->channels, musicbrainz->depth);
  musicbrainz->linked= TRUE;

  return GST_PAD_LINK_OK; 
}


static void
gst_musicbrainz_init (GstMusicBrainz *musicbrainz)
{
  musicbrainz->sinkpad = gst_pad_new_from_template (
		  gst_static_pad_template_get (&sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (musicbrainz), musicbrainz->sinkpad);
  gst_pad_set_chain_function (musicbrainz->sinkpad, gst_musicbrainz_chain);
  gst_pad_set_link_function (musicbrainz->sinkpad, gst_musicbrainz_sinkconnect);

  musicbrainz->srcpad = gst_pad_new_from_template (
		  gst_static_pad_template_get (&src_template), "src");
  gst_element_add_pad (GST_ELEMENT (musicbrainz), musicbrainz->srcpad);

  musicbrainz->trm = NULL;
  musicbrainz->linked = FALSE;
  musicbrainz->data_available = FALSE;
  musicbrainz->total_time = 0;
  musicbrainz->signature_available = FALSE;

  GST_FLAG_SET (musicbrainz, GST_ELEMENT_EVENT_AWARE);
  /*GST_FLAG_SET(musicbrainz, GST_ELEMENT_THREAD_SUGGESTED);*/
}

static void
gst_trm_handle_event (GstPad *pad, GstData *data)
{
  GstEvent *event = GST_EVENT (data);

  gst_pad_event_default (pad, event);
}

static void
gst_musicbrainz_chain (GstPad *pad, GstData *data)
{
  GstMusicBrainz *musicbrainz;
  GstBuffer *buf;
  static GstFormat format = GST_FORMAT_TIME;
  gint64 nanos;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  musicbrainz = GST_MUSICBRAINZ (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (data))
  {
    gst_trm_handle_event (pad, data);
    return;
  }

  buf = GST_BUFFER (data);
  
  if (musicbrainz->linked && !musicbrainz->data_available)
    if (gst_pad_query (gst_pad_get_peer (pad), GST_QUERY_TOTAL, &format, &nanos))
    {
      musicbrainz->total_time = nanos / GST_SECOND;
      trm_SetSongLength(musicbrainz->trm, musicbrainz->total_time);
      musicbrainz->data_available = TRUE;

      gst_pad_try_set_caps (musicbrainz->srcpad, musicbrainz->caps);
    }

  if (!musicbrainz->signature_available && trm_GenerateSignature (musicbrainz->trm, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf)))
  {
    GST_DEBUG ("Signature");

    trm_FinalizeSignature(musicbrainz->trm, musicbrainz->signature, NULL);
    trm_ConvertSigToASCII (musicbrainz->trm, musicbrainz->signature, musicbrainz->ascii_signature);
g_print ("Signature : %s\n", musicbrainz->ascii_signature);
    musicbrainz->signature_available = TRUE;
    g_signal_emit (G_OBJECT(musicbrainz),gst_musicbrainz_signals[SIGNAL_SIGNATURE_AVAILABLE], 0);

    GST_DEBUG ("Signature : %s", musicbrainz->ascii_signature);

    musicbrainz->signature_available = TRUE;
  }

  gst_pad_push (musicbrainz->srcpad, data);
}


static void
gst_musicbrainz_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMusicBrainz *musicbrainz;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MUSICBRAINZ (object));

  musicbrainz = GST_MUSICBRAINZ (object);

  switch (prop_id) {
    case ARG_SIGNATURE:
    case ARG_ASCII_SIGNATURE:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_musicbrainz_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMusicBrainz *musicbrainz;

  /* it's not null if we got it, but it might not be ours */
  musicbrainz = GST_MUSICBRAINZ(object);

  switch (prop_id) {
    case ARG_SIGNATURE: {
      g_value_set_string (value, musicbrainz->signature);
      break;
    }
    case ARG_ASCII_SIGNATURE: {
      g_value_set_string (value, musicbrainz->ascii_signature);
      break;
    }
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}


static GstElementStateReturn
gst_musicbrainz_change_state (GstElement *element)
{
  GstMusicBrainz *musicbrainz;

  g_return_val_if_fail (GST_IS_MUSICBRAINZ (element), GST_STATE_FAILURE);

  musicbrainz = GST_MUSICBRAINZ (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      musicbrainz->trm = trm_New ();
      break;
    case GST_STATE_PAUSED_TO_READY:
      trm_Delete (musicbrainz->trm);
      musicbrainz->trm = NULL;
      musicbrainz->linked = FALSE;
      musicbrainz->data_available = FALSE;
      musicbrainz->total_time = 0;
      musicbrainz->signature_available = FALSE;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "trm",
			       GST_RANK_NONE,
			       GST_TYPE_MUSICBRAINZ);
}


GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "trm",
  "A trm signature producer",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN
)
