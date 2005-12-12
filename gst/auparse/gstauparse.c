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
/* Element-Checklist-Version: 5 */

/* 2001/04/03 - Updated parseau to use caps nego
 *              Zaheer Abbas Merali <zaheerabbas at merali dot org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "gstauparse.h"
#include <gst/audio/audio.h>

/* elementfactory information */
static GstElementDetails gst_au_parse_details =
GST_ELEMENT_DETAILS (".au parser",
    "Codec/Demuxer/Audio",
    "Parse an .au file into raw audio",
    "Erik Walthinsen <omega@cse.ogi.edu>");

static GstStaticPadTemplate gst_au_parse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-au")
    );

#define GST_AU_PARSE_ALAW_PAD_TEMPLATE_CAPS \
    "audio/x-alaw, "                        \
    "rate = (int) [ 8000, 192000 ], "       \
    "channels = (int) [ 1, 2 ]"

#define GST_AU_PARSE_MULAW_PAD_TEMPLATE_CAPS \
    "audio/x-mulaw, "                        \
    "rate = (int) [ 8000, 192000 ], "        \
    "channels = (int) [ 1, 2 ]"

/* Nothing to decode those ADPCM streams for now */
#define GST_AU_PARSE_ADPCM_PAD_TEMPLATE_CAPS \
    "audio/x-adpcm, "                        \
    "layout = (string) { g721, g722, g723_3, g723_5 }"

static GstStaticPadTemplate gst_au_parse_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS ";"
        GST_AU_PARSE_ALAW_PAD_TEMPLATE_CAPS ";"
        GST_AU_PARSE_MULAW_PAD_TEMPLATE_CAPS ";"
        GST_AU_PARSE_ADPCM_PAD_TEMPLATE_CAPS));


static void gst_au_parse_dispose (GObject * object);
static GstFlowReturn gst_au_parse_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_au_parse_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstAuParse, gst_au_parse, GstElement, GST_TYPE_ELEMENT)

     static void gst_au_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_au_parse_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_au_parse_src_template));
  gst_element_class_set_details (element_class, &gst_au_parse_details);

}

static void
gst_au_parse_class_init (GstAuParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_au_parse_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_au_parse_change_state);
}

static void
gst_au_parse_init (GstAuParse * auparse, GstAuParseClass * klass)
{
  auparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_au_parse_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->sinkpad);
  gst_pad_set_chain_function (auparse->sinkpad, gst_au_parse_chain);

  auparse->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_au_parse_src_template), "src");
  gst_pad_use_fixed_caps (auparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->srcpad);

  auparse->offset = 0;
  auparse->buffer_offset = 0;
  auparse->adapter = gst_adapter_new ();
  auparse->size = 0;
  auparse->encoding = 0;
  auparse->frequency = 0;
  auparse->channels = 0;
}

static void
gst_au_parse_dispose (GObject * object)
{
  GstAuParse *au = GST_AU_PARSE (object);

  if (au->adapter != NULL) {
    gst_object_unref (au->adapter);
    au->adapter = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstFlowReturn
gst_au_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstAuParse *auparse;
  guchar *data;
  glong size;
  GstCaps *tempcaps;
  gint law = 0, depth = 0, ieee = 0;
  gchar layout[7];
  GstBuffer *subbuf;
  GstEvent *event;

  layout[0] = 0;

  auparse = GST_AU_PARSE (gst_pad_get_parent (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG_OBJECT (auparse, "got buffer of size %ld", size);

  /* if we haven't seen any data yet... */
  if (auparse->size == 0) {
    guint32 *head = (guint32 *) data;

    /* normal format is big endian (au is a Sparc format) */
    if (GST_READ_UINT32_BE (head) == 0x2e736e64) {      /* ".snd" */
      head++;
      auparse->le = 0;
      auparse->offset = GST_READ_UINT32_BE (head);
      head++;
      /* Do not trust size, could be set to -1 : unknown */
      auparse->size = GST_READ_UINT32_BE (head);
      head++;
      auparse->encoding = GST_READ_UINT32_BE (head);
      head++;
      auparse->frequency = GST_READ_UINT32_BE (head);
      head++;
      auparse->channels = GST_READ_UINT32_BE (head);
      head++;

      /* and of course, someone had to invent a little endian
       * version.  Used by DEC systems. */
    } else if (GST_READ_UINT32_LE (head) == 0x0064732E) {       /* other source say it is "dns." */
      head++;
      auparse->le = 1;
      auparse->offset = GST_READ_UINT32_LE (head);
      head++;
      /* Do not trust size, could be set to -1 : unknown */
      auparse->size = GST_READ_UINT32_LE (head);
      head++;
      auparse->encoding = GST_READ_UINT32_LE (head);
      head++;
      auparse->frequency = GST_READ_UINT32_LE (head);
      head++;
      auparse->channels = GST_READ_UINT32_LE (head);
      head++;

    } else {
      GST_ELEMENT_ERROR (auparse, STREAM, WRONG_TYPE, (NULL), (NULL));
      gst_buffer_unref (buf);
      gst_object_unref (auparse);
      return GST_FLOW_ERROR;
    }

    GST_DEBUG
        ("offset %ld, size %ld, encoding %ld, frequency %ld, channels %ld\n",
        auparse->offset, auparse->size, auparse->encoding, auparse->frequency,
        auparse->channels);

/*
Docs :
        http://www.opengroup.org/public/pubs/external/auformat.html
        http://astronomy.swin.edu.au/~pbourke/dataformats/au/
        Solaris headers : /usr/include/audio/au.h
        libsndfile : src/au.c
Samples :
        http://www.tsp.ece.mcgill.ca/MMSP/Documents/AudioFormats/AU/Samples.html
*/

    switch (auparse->encoding) {

      case 1:                  /* 8-bit ISDN mu-law G.711 */
        law = 1;
        depth = 8;
        break;
      case 27:                 /* 8-bit ISDN  A-law G.711 */
        law = 2;
        depth = 8;
        break;

      case 2:                  /*  8-bit linear PCM */
        depth = 8;
        break;
      case 3:                  /* 16-bit linear PCM */
        depth = 16;
        break;
      case 4:                  /* 24-bit linear PCM */
        depth = 24;
        break;
      case 5:                  /* 32-bit linear PCM */
        depth = 32;
        break;

      case 6:                  /* 32-bit IEEE floating point */
        ieee = 1;
        depth = 32;
        break;
      case 7:                  /* 64-bit IEEE floating point */
        ieee = 1;
        depth = 64;
        break;

      case 23:                 /* 4-bit CCITT G.721   ADPCM 32kbps -> modplug/libsndfile (compressed 8-bit mu-law) */
        strcpy (layout, "g721");
        break;
      case 24:                 /* 8-bit CCITT G.722   ADPCM        -> rtp */
        strcpy (layout, "g722");
        break;
      case 25:                 /* 3-bit CCITT G.723.3 ADPCM 24kbps -> rtp/xine/modplug/libsndfile */
        strcpy (layout, "g723_3");
        break;
      case 26:                 /* 5-bit CCITT G.723.5 ADPCM 40kbps -> rtp/xine/modplug/libsndfile */
        strcpy (layout, "g723_5");
        break;

      case 8:                  /* Fragmented sample data */
      case 9:                  /* AU_ENCODING_NESTED */

      case 10:                 /* DSP program */
      case 11:                 /* DSP  8-bit fixed point */
      case 12:                 /* DSP 16-bit fixed point */
      case 13:                 /* DSP 24-bit fixed point */
      case 14:                 /* DSP 32-bit fixed point */

      case 16:                 /* AU_ENCODING_DISPLAY : non-audio display data */
      case 17:                 /* AU_ENCODING_MULAW_SQUELCH */

      case 18:                 /* 16-bit linear with emphasis */
      case 19:                 /* 16-bit linear compressed (NeXT) */
      case 20:                 /* 16-bit linear with emphasis and compression */

      case 21:                 /* Music kit DSP commands */
      case 22:                 /* Music kit DSP commands samples */

      default:
        GST_ELEMENT_ERROR (auparse, STREAM, FORMAT, (NULL), (NULL));
        gst_buffer_unref (buf);
        gst_object_unref (auparse);
        return GST_FLOW_ERROR;
    }

    if (law) {
      tempcaps =
          gst_caps_new_simple ((law == 1) ? "audio/x-mulaw" : "audio/x-alaw",
          "rate", G_TYPE_INT, auparse->frequency,
          "channels", G_TYPE_INT, auparse->channels, NULL);
      auparse->sample_size = auparse->channels;
    } else if (ieee) {
      tempcaps = gst_caps_new_simple ("audio/x-raw-float",
          "rate", G_TYPE_INT, auparse->frequency,
          "channels", G_TYPE_INT, auparse->channels,
          "endianness", G_TYPE_INT,
          auparse->le ? G_LITTLE_ENDIAN : G_BIG_ENDIAN,
          "width", G_TYPE_INT, depth, NULL);
      auparse->sample_size = auparse->channels * depth / 8;
    } else if (layout[0]) {
      tempcaps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      auparse->sample_size = 0;
    } else {
      tempcaps = gst_caps_new_simple ("audio/x-raw-int",
          "rate", G_TYPE_INT, auparse->frequency,
          "channels", G_TYPE_INT, auparse->channels,
          "endianness", G_TYPE_INT,
          auparse->le ? G_LITTLE_ENDIAN : G_BIG_ENDIAN, "depth", G_TYPE_INT,
          depth, "width", G_TYPE_INT, depth, "signed", G_TYPE_BOOLEAN, TRUE,
          NULL);
      auparse->sample_size = auparse->channels * depth / 8;
    }

    gst_pad_set_active (auparse->srcpad, TRUE);
    gst_pad_set_caps (auparse->srcpad, tempcaps);

    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_DEFAULT,
        0, GST_CLOCK_TIME_NONE, 0);

    gst_pad_push_event (auparse->srcpad, event);

    subbuf = gst_buffer_create_sub (buf, auparse->offset,
        size - auparse->offset);

    gst_buffer_unref (buf);

    gst_adapter_push (auparse->adapter, subbuf);
  } else {
    gst_adapter_push (auparse->adapter, buf);
  }

  if (auparse->sample_size) {
    /* Ensure we push a buffer that's a multiple of the frame size downstream */
    int avail = gst_adapter_available (auparse->adapter);

    avail -= avail % auparse->sample_size;

    if (avail > 0) {
      const guint8 *data = gst_adapter_peek (auparse->adapter, avail);
      GstBuffer *newbuf;

      if ((ret =
              gst_pad_alloc_buffer_and_set_caps (auparse->srcpad,
                  auparse->buffer_offset, avail, GST_PAD_CAPS (auparse->srcpad),
                  &newbuf)) == GST_FLOW_OK) {

        memcpy (GST_BUFFER_DATA (newbuf), data, avail);
        gst_adapter_flush (auparse->adapter, avail);

        auparse->buffer_offset += avail;

        ret = gst_pad_push (auparse->srcpad, newbuf);
      }
    } else
      ret = GST_FLOW_OK;
  } else {
    /* It's something non-trivial (such as ADPCM), we don't understand it, so
     * just push downstream and assume this will know what to do with it */
    ret = gst_pad_push (auparse->srcpad, buf);
  }

  gst_object_unref (auparse);

  return ret;
}

static GstStateChangeReturn
gst_au_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstAuParse *auparse = GST_AU_PARSE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_adapter_clear (auparse->adapter);
      auparse->buffer_offset = 0;
      auparse->offset = 0;
      auparse->size = 0;
      auparse->encoding = 0;
      auparse->frequency = 0;
      auparse->channels = 0;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "auparse", GST_RANK_SECONDARY,
          GST_TYPE_AU_PARSE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "auparse",
    "parses au streams", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
