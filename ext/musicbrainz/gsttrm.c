/* GStreamer trm plugin
 * Copyright (C) 2004 Jeremy Simon <jsimon13@yahoo.fr>
 * Copyright (C) 2006 James Livingston <doclivingston gmail com>
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

/**
 * SECTION:element-trm
 *
 * GstTRM computes <ulink url="http://www.musicbrainz.org/">MusicBrainz</ulink>
 * TRM identifiers for audio streams using libmusicbrainz.
 * 
 * A TRM identifier is something like an 'acoustic fingerprint', the aim is
 * to uniquely identify the same song regardless of which source it comes from
 * or which audio format the stream is in.
 * 
 * The TRM element will collect about 30 seconds of audio and let
 * libmusicbrainz calculate a preliminary audio signature from that. That audio
 * signature will then be sent over the internet to a musicbrainz.org server
 * which will calculate the TRM for that signature.
 * 
 * The TRM element will post a tag message with a #GST_TAG_MUSICBRAINZ_TRMID
 * tag on the bus once the TRM has been calculated (and also send a tag event
 * with that information downstream).
 * 
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -m filesrc location=somefile.ogg ! decodebin ! audioconvert ! trm ! fakesink
 * ]| calculate the TRM and print the tag message with the TRM ID.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/time.h>

#include "gsttrm.h"

enum
{
  ARG_0,
  ARG_PROXY_ADDRESS,
  ARG_PROXY_PORT
};

GST_DEBUG_CATEGORY_STATIC (trm_debug);
#define GST_CAT_DEFAULT trm_debug

GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (bool) TRUE, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );


GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (bool) TRUE, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );

#define DEFAULT_PROXY_ADDRESS  NULL
#define DEFAULT_PROXY_PORT     8080

static GstFlowReturn gst_trm_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_trm_setcaps (GstPad * pad, GstCaps * caps);

static void gst_trm_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_trm_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_trm_change_state (GstElement * element, GstStateChange transition);

static void gst_trm_emit_signature (GstTRM * trm);


GST_BOILERPLATE (GstTRM, gst_trm, GstElement, GST_TYPE_ELEMENT);

static void
gst_trm_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);

  gst_element_class_set_details_simple (element_class,
      "MusicBrainz TRM generator", "Filter/Analyzer/Audio",
      "Compute MusicBrainz TRM Id using libmusicbrainz",
      "Jeremy Simon <jsimon13@yahoo.fr>");
}

static void
gst_trm_class_init (GstTRMClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_trm_set_property;
  gobject_class->get_property = gst_trm_get_property;

  g_object_class_install_property (gobject_class, ARG_PROXY_ADDRESS,
      g_param_spec_string ("proxy-address", "proxy address", "proxy address",
          DEFAULT_PROXY_ADDRESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PROXY_PORT,
      g_param_spec_uint ("proxy-port", "proxy port", "proxy port",
          1, 65535, DEFAULT_PROXY_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_trm_change_state);
}

static void
gst_trm_init (GstTRM * trm, GstTRMClass * klass)
{
  trm->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (trm->sinkpad, GST_DEBUG_FUNCPTR (gst_trm_chain));
  gst_pad_set_setcaps_function (trm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_trm_setcaps));
  gst_element_add_pad (GST_ELEMENT (trm), trm->sinkpad);

  trm->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (trm), trm->srcpad);

  trm->proxy_address = g_strdup (DEFAULT_PROXY_ADDRESS);
  trm->proxy_port = DEFAULT_PROXY_PORT;

  trm->trm = NULL;
  trm->data_available = FALSE;
  trm->signature_available = FALSE;
}

static gboolean
gst_trm_setcaps (GstPad * pad, GstCaps * caps)
{
  GstTRM *trm;
  GstStructure *structure;
  gint width;

  trm = GST_TRM (gst_pad_get_parent (pad));

  if (!gst_pad_set_caps (trm->srcpad, caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "depth", &trm->depth) ||
      !gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "channels", &trm->channels) ||
      !gst_structure_get_int (structure, "rate", &trm->rate)) {
    GST_DEBUG_OBJECT (trm, "failed to extract depth, width, channels or rate");
    goto failure;
  }

  if (trm->depth != width) {
    GST_DEBUG_OBJECT (trm, "depth != width (%d != %d)", trm->depth, width);
    goto failure;
  }

  trm_SetPCMDataInfo (trm->trm, trm->rate, trm->channels, trm->depth);

  gst_object_unref (trm);
  return TRUE;

/* ERRORS */
failure:
  {
    GST_WARNING_OBJECT (trm, "FAILED with caps %" GST_PTR_FORMAT, caps);
    gst_object_unref (trm);
    return FALSE;
  }
}

static GstFlowReturn
gst_trm_chain (GstPad * pad, GstBuffer * buf)
{
  GstTRM *trm = GST_TRM (GST_PAD_PARENT (pad));

  if (!trm->data_available) {
    GstFormat tformat = GST_FORMAT_TIME;
    gint64 total_duration;

    /* FIXME: maybe we should only query this once we have as much data as
     * we need (30secs or so), to get a better estimation of the length in
     * the case of VBR files? */
    if (gst_pad_query_peer_duration (pad, &tformat, &total_duration)) {
      total_duration /= GST_SECOND;
      trm_SetSongLength (trm->trm, total_duration);
      trm->data_available = TRUE;
    }
  }

  if (!trm->signature_available
      && trm_GenerateSignature (trm->trm, (char *) GST_BUFFER_DATA (buf),
          GST_BUFFER_SIZE (buf))) {
    GST_DEBUG ("Signature");

    GST_OBJECT_LOCK (trm);
    if (trm->proxy_address != NULL) {
      if (!trm_SetProxy (trm->trm, trm->proxy_address, trm->proxy_port)) {
        GST_OBJECT_UNLOCK (trm);
        goto proxy_setup_error;
      }
    }
    GST_OBJECT_UNLOCK (trm);

    gst_trm_emit_signature (trm);
  }

  return gst_pad_push (trm->srcpad, buf);

/* ERRORS */
proxy_setup_error:
  {
    GST_ELEMENT_ERROR (trm, RESOURCE, SETTINGS, (NULL),
        ("Unable to set proxy server for trm lookup"));
    return GST_FLOW_ERROR;
  }
}

static void
gst_trm_emit_signature (GstTRM * trm)
{
  char signature[17];
  char ascii_sig[37];

  if (trm->signature_available)
    return;

  if (trm_FinalizeSignature (trm->trm, signature, NULL) == 0) {
    GstTagList *tags;

    trm_ConvertSigToASCII (trm->trm, signature, ascii_sig);
    ascii_sig[36] = '\0';
    GST_DEBUG_OBJECT (trm, "Signature : %s", ascii_sig);

    tags = gst_tag_list_new ();
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_MUSICBRAINZ_TRMID, ascii_sig, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (trm), trm->srcpad, tags);

    trm->signature_available = TRUE;
  } else {
    /* FIXME: should we be throwing an error here? */
  }
}


static void
gst_trm_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTRM *trm;

  trm = GST_TRM (object);

  GST_OBJECT_LOCK (trm);

  switch (prop_id) {
    case ARG_PROXY_ADDRESS:{
      g_free (trm->proxy_address);
      trm->proxy_address = g_value_dup_string (value);
      break;
    }
    case ARG_PROXY_PORT:{
      trm->proxy_port = g_value_get_uint (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (trm);
}

static void
gst_trm_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTRM *trm;

  trm = GST_TRM (object);

  GST_OBJECT_LOCK (trm);

  switch (prop_id) {
    case ARG_PROXY_ADDRESS:{
      g_value_set_string (value, trm->proxy_address);
      break;
    }
    case ARG_PROXY_PORT:{
      g_value_set_uint (value, trm->proxy_port);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }

  GST_OBJECT_UNLOCK (trm);
}

static GstStateChangeReturn
gst_trm_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstTRM *trm;

  trm = GST_TRM (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      trm->trm = trm_New ();
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      trm_Delete (trm->trm);
      trm->trm = NULL;
      trm->data_available = FALSE;
      trm->signature_available = FALSE;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_tag_register_musicbrainz_tags ();

  if (!gst_element_register (plugin, "trm", GST_RANK_NONE, GST_TYPE_TRM))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (trm_debug, "trm", 0, "TRM calculation element");

  return TRUE;
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "musicbrainz",
    "A TRM signature producer based on libmusicbrainz",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
