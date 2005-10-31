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
static GstElementDetails gst_auparse_details =
GST_ELEMENT_DETAILS (".au parser",
    "Codec/Demuxer/Audio",
    "Parse an .au file into raw audio",
    "Erik Walthinsen <omega@cse.ogi.edu>");

static GstStaticPadTemplate gst_auparse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-au")
    );

static GstStaticPadTemplate gst_auparse_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,          /* FIXME: spider */
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
        /* we don't use GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS
           because of min buffer-frames which is 1, not 0 */
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }, "
        "buffer-frames = (int) [ 0, MAX]" "; "
        "audio/x-alaw, "
        "rate = (int) [ 8000, 192000 ], "
        "channels = (int) [ 1, 2 ]" "; "
        "audio/x-mulaw, "
        "rate = (int) [ 8000, 192000 ], " "channels = (int) [ 1, 2 ]" "; "
        /* Nothing to decode those ADPCM streams for now */
        "audio/x-adpcm, " "layout = (string) { g721, g722, g723_3, g723_5 }")
    );

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_auparse_base_init (gpointer g_class);
static void gst_auparse_class_init (GstAuParseClass * klass);
static void gst_auparse_init (GstAuParse * auparse);

static GstFlowReturn gst_auparse_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn gst_auparse_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;


/*static guint gst_auparse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_auparse_get_type (void)
{
  static GType auparse_type = 0;

  if (!auparse_type) {
    static const GTypeInfo auparse_info = {
      sizeof (GstAuParseClass),
      gst_auparse_base_init,
      NULL,
      (GClassInitFunc) gst_auparse_class_init,
      NULL,
      NULL,
      sizeof (GstAuParse),
      0,
      (GInstanceInitFunc) gst_auparse_init,
    };

    auparse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAuParse", &auparse_info,
        0);
  }
  return auparse_type;
}

static void
gst_auparse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_auparse_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_auparse_src_template));
  gst_element_class_set_details (element_class, &gst_auparse_details);

}

static void
gst_auparse_class_init (GstAuParseClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_auparse_change_state;
}

static void
gst_auparse_init (GstAuParse * auparse)
{
  auparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_auparse_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->sinkpad);
  gst_pad_set_chain_function (auparse->sinkpad, gst_auparse_chain);

  auparse->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_auparse_src_template), "src");
  gst_pad_use_fixed_caps (auparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->srcpad);

  auparse->offset = 0;
  auparse->size = 0;
  auparse->encoding = 0;
  auparse->frequency = 0;
  auparse->channels = 0;
}

static GstFlowReturn
gst_auparse_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstAuParse *auparse;
  guchar *data;
  glong size;
  GstCaps *tempcaps;
  gint law = 0, depth = 0, ieee = 0;
  gchar layout[7];

  layout[0] = 0;

  auparse = GST_AUPARSE (gst_pad_get_parent (pad));

  GST_DEBUG ("gst_auparse_chain: got buffer in '%s'",
      gst_element_get_name (GST_ELEMENT (auparse)));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* if we haven't seen any data yet... */
  if (auparse->size == 0) {
    GstBuffer *newbuf;
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
      g_object_unref (auparse);
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
        g_object_unref (auparse);
        return GST_FLOW_ERROR;
    }

    if (law) {
      tempcaps =
          gst_caps_new_simple ((law == 1) ? "audio/x-mulaw" : "audio/x-alaw",
          "rate", G_TYPE_INT, auparse->frequency,
          "channels", G_TYPE_INT, auparse->channels, NULL);
    } else if (ieee) {
      tempcaps = gst_caps_new_simple ("audio/x-raw-float",
          "rate", G_TYPE_INT, auparse->frequency,
          "channels", G_TYPE_INT, auparse->channels,
          "endianness", G_TYPE_INT,
          auparse->le ? G_LITTLE_ENDIAN : G_BIG_ENDIAN, "width", G_TYPE_INT,
          depth, "buffer-frames", G_TYPE_INT, 0, NULL);
    } else if (layout[0]) {
      tempcaps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, layout, NULL);
    } else {
      tempcaps = gst_caps_new_simple ("audio/x-raw-int",
          "rate", G_TYPE_INT, auparse->frequency,
          "channels", G_TYPE_INT, auparse->channels,
          "endianness", G_TYPE_INT,
          auparse->le ? G_LITTLE_ENDIAN : G_BIG_ENDIAN, "depth", G_TYPE_INT,
          depth, "width", G_TYPE_INT, depth, "signed", G_TYPE_BOOLEAN, TRUE,
          NULL);
    }

    gst_pad_set_active (auparse->srcpad, TRUE);
    gst_pad_set_caps (auparse->srcpad, tempcaps);

    if ((ret = gst_pad_alloc_buffer (auparse->srcpad, GST_BUFFER_OFFSET_NONE,
                size - (auparse->offset),
                GST_PAD_CAPS (auparse->srcpad), &newbuf)) != GST_FLOW_OK) {
      gst_buffer_unref (buf);
      g_object_unref (auparse);
      return ret;
    }
    ret = GST_FLOW_OK;


    memcpy (GST_BUFFER_DATA (newbuf), data + (auparse->offset),
        size - (auparse->offset));
    GST_BUFFER_SIZE (newbuf) = size - (auparse->offset);

    GstEvent *event;

    event = NULL;

    event = gst_event_new_newsegment (FALSE, 1.0, GST_FORMAT_DEFAULT,
        0, GST_CLOCK_TIME_NONE, 0);


    gst_pad_push_event (auparse->srcpad, event);

    gst_buffer_unref (buf);
    g_object_unref (auparse);
    return gst_pad_push (auparse->srcpad, newbuf);

  }

  g_object_unref (auparse);
  return gst_pad_push (auparse->srcpad, buf);

}

static GstStateChangeReturn
gst_auparse_change_state (GstElement * element, GstStateChange transition)
{
  GstAuParse *auparse = GST_AUPARSE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  if (parent_class->change_state)
    ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
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
          GST_TYPE_AUPARSE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "auparse",
    "parses au streams", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
