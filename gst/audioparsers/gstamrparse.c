/* GStreamer Adaptive Multi-Rate parser plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * SECTION:gstamrparse
 * @short_description: AMR parser
 * @see_also: #GstAmrnbDec, #GstAmrnbEnc
 *
 * <refsect2>
 * <para>
 * This is an AMR parser capable of handling both narrow-band and wideband
 * formats.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=abc.amr ! amrparse ! amrdec ! audioresample ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamrparse.h"


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1;"
        "audio/AMR-WB, " "rate = (int) 16000, " "channels = (int) 1;")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-nb-sh; audio/x-amr-wb-sh"));

GST_DEBUG_CATEGORY_STATIC (gst_amrparse_debug);
#define GST_CAT_DEFAULT gst_amrparse_debug

static const gint block_size_nb[16] =
    { 12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0 };

static const gint block_size_wb[16] =
    { 17, 23, 32, 36, 40, 46, 50, 58, 60, 5, 5, 0, 0, 0, 0, 0 };

/* AMR has a "hardcoded" framerate of 50fps */
#define AMR_FRAMES_PER_SECOND 50
#define AMR_FRAME_DURATION (GST_SECOND/AMR_FRAMES_PER_SECOND)
#define AMR_MIME_HEADER_SIZE 9

static void gst_amrparse_finalize (GObject * object);

gboolean gst_amrparse_start (GstBaseParse * parse);
gboolean gst_amrparse_stop (GstBaseParse * parse);

static gboolean gst_amrparse_sink_setcaps (GstBaseParse * parse,
    GstCaps * caps);

gboolean gst_amrparse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * framesize, gint * skipsize);

GstFlowReturn gst_amrparse_parse_frame (GstBaseParse * parse,
    GstBuffer * buffer);

gboolean gst_amrparse_convert (GstBaseParse * parse,
    GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value);

gboolean gst_amrparse_event (GstBaseParse * parse, GstEvent * event);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_amrparse_debug, "amrparse", 0, \
                             "AMR-NB audio stream parser");

GST_BOILERPLATE_FULL (GstAmrParse, gst_amrparse, GstBaseParse,
    GST_TYPE_BASE_PARSE, _do_init);


/**
 * gst_amrparse_base_init:
 * @klass: #GstElementClass.
 *
 */
static void
gst_amrparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details = GST_ELEMENT_DETAILS ("AMR audio stream parser",
      "Codec/Parser/Audio",
      "Adaptive Multi-Rate audio parser",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &details);
}


/**
 * gst_amrparse_class_init:
 * @klass: GstAmrParseClass.
 *
 */
static void
gst_amrparse_class_init (GstAmrParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  object_class->finalize = gst_amrparse_finalize;

  parse_class->start = GST_DEBUG_FUNCPTR (gst_amrparse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_amrparse_stop);
  parse_class->event = GST_DEBUG_FUNCPTR (gst_amrparse_event);
  parse_class->convert = GST_DEBUG_FUNCPTR (gst_amrparse_convert);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_amrparse_sink_setcaps);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_amrparse_parse_frame);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_amrparse_check_valid_frame);
}


/**
 * gst_amrparse_init:
 * @amrparse: #GstAmrParse
 * @klass: #GstAmrParseClass.
 *
 */
static void
gst_amrparse_init (GstAmrParse * amrparse, GstAmrParseClass * klass)
{
  /* init rest */
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (amrparse), 62);
  amrparse->ts = 0;
  GST_DEBUG ("initialized");

}


/**
 * gst_amrparse_finalize:
 * @object:
 *
 */
static void
gst_amrparse_finalize (GObject * object)
{
  GstAmrParse *amrparse;

  amrparse = GST_AMRPARSE (object);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * gst_amrparse_set_src_caps:
 * @amrparse: #GstAmrParse.
 *
 * Set source pad caps according to current knowledge about the
 * audio stream.
 *
 * Returns: TRUE if caps were successfully set.
 */
static gboolean
gst_amrparse_set_src_caps (GstAmrParse * amrparse)
{
  GstCaps *src_caps = NULL;
  gboolean res = FALSE;

  if (amrparse->wide) {
    GST_DEBUG_OBJECT (amrparse, "setting srcpad caps to AMR-WB");
    src_caps = gst_caps_new_simple ("audio/AMR-WB",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 16000, NULL);
  } else {
    GST_DEBUG_OBJECT (amrparse, "setting srcpad caps to AMR-NB");
    /* Max. size of NB frame is 31 bytes, so we can set the min. frame
       size to 32 (+1 for next frame header) */
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (amrparse), 32);
    src_caps = gst_caps_new_simple ("audio/AMR",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
  }
  gst_pad_use_fixed_caps (GST_BASE_PARSE (amrparse)->srcpad);
  res = gst_pad_set_caps (GST_BASE_PARSE (amrparse)->srcpad, src_caps);
  gst_caps_unref (src_caps);
  return res;
}


/**
 * gst_amrparse_sink_setcaps:
 * @sinkpad: GstPad
 * @caps: GstCaps
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_amrparse_sink_setcaps (GstBaseParse * parse, GstCaps * caps)
{
  GstAmrParse *amrparse;
  GstStructure *structure;
  const gchar *name;

  amrparse = GST_AMRPARSE (parse);
  structure = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (structure);

  GST_DEBUG_OBJECT (amrparse, "setcaps: %s", name);

  if (!strncmp (name, "audio/x-amr-wb-sh", 17)) {
    amrparse->block_size = block_size_wb;
    amrparse->wide = 1;
  } else if (!strncmp (name, "audio/x-amr-nb-sh", 17)) {
    amrparse->block_size = block_size_nb;
    amrparse->wide = 0;
  } else {
    GST_WARNING ("Unknown caps");
    return FALSE;
  }

  amrparse->need_header = FALSE;
  gst_amrparse_set_src_caps (amrparse);
  return TRUE;
}


/**
 * gst_amrparse_update_duration:
 * @amrparse: #GstAmrParse.
 *
 * Send duration information to base class.
 */
static void
gst_amrparse_update_duration (GstAmrParse * amrparse)
{
  GstPad *peer;
  GstBaseParse *parse;

  parse = GST_BASE_PARSE (amrparse);

  /* Cannot estimate duration. No data has been passed to us yet */
  if (!amrparse->framecount) {
    return;
  }

  peer = gst_pad_get_peer (parse->sinkpad);
  if (peer) {
    GstFormat pformat = GST_FORMAT_BYTES;
    guint64 bpf = amrparse->bytecount / amrparse->framecount;
    gboolean qres = FALSE;
    gint64 ptot;

    qres = gst_pad_query_duration (peer, &pformat, &ptot);
    gst_object_unref (GST_OBJECT (peer));
    if (qres && bpf) {
      gst_base_parse_set_duration (parse, GST_FORMAT_TIME,
          AMR_FRAME_DURATION * ptot / bpf);
    }
  }
}


/**
 * gst_amrparse_parse_header:
 * @amrparse: #GstAmrParse
 * @data: Header data to be parsed.
 * @skipsize: Output argument where the frame size will be stored.
 *
 * Check if the given data contains an AMR mime header.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_amrparse_parse_header (GstAmrParse * amrparse,
    const guint8 * data, gint * skipsize)
{
  GST_DEBUG_OBJECT (amrparse, "Parsing header data");

  if (!memcmp (data, "#!AMR-WB\n", 9)) {
    GST_DEBUG_OBJECT (amrparse, "AMR-WB detected");
    amrparse->block_size = block_size_wb;
    amrparse->wide = TRUE;
    *skipsize = amrparse->header = 9;
  } else if (!memcmp (data, "#!AMR\n", 6)) {
    GST_DEBUG_OBJECT (amrparse, "AMR-NB detected");
    amrparse->block_size = block_size_nb;
    amrparse->wide = FALSE;
    *skipsize = amrparse->header = 6;
  } else
    return FALSE;

  gst_amrparse_set_src_caps (amrparse);
  return TRUE;
}


/**
 * gst_amrparse_check_valid_frame:
 * @parse: #GstBaseParse.
 * @buffer: #GstBuffer.
 * @framesize: Output variable where the found frame size is put.
 * @skipsize: Output variable which tells how much data needs to be skipped
 *            until a frame header is found.
 *
 * Implementation of "check_valid_frame" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if the given data contains valid frame.
 */
gboolean
gst_amrparse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * framesize, gint * skipsize)
{
  const guint8 *data;
  gint fsize, mode, dsize;
  GstAmrParse *amrparse;

  amrparse = GST_AMRPARSE (parse);
  data = GST_BUFFER_DATA (buffer);
  dsize = GST_BUFFER_SIZE (buffer);

  GST_LOG ("buffer: %d bytes", dsize);

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    /* Discontinuous stream -> drop the sync */
    amrparse->sync = FALSE;
  }

  if (amrparse->need_header) {
    if (dsize >= AMR_MIME_HEADER_SIZE &&
        gst_amrparse_parse_header (amrparse, data, skipsize)) {
      amrparse->need_header = FALSE;
    } else {
      GST_WARNING ("media doesn't look like a AMR format");
    }
    /* We return FALSE, so this frame won't get pushed forward. Instead,
       the "skip" value is set, so next time we will receive a valid frame. */
    return FALSE;
  }

  /* Does this look like a possible frame header candidate? */
  if ((data[0] & 0x83) == 0) {
    /* Yep. Retrieve the frame size */
    mode = (data[0] >> 3) & 0x0F;
    fsize = amrparse->block_size[mode] + 1;     /* +1 for the header byte */

    /* We recognize this data as a valid frame when:
     *     - We are in sync. There is no need for extra checks then
     *     - We are in EOS. There might not be enough data to check next frame
     *     - Sync is lost, but the following data after this frame seem
     *       to contain a valid header as well (and there is enough data to
     *       perform this check)
     */
    if (amrparse->sync || amrparse->eos ||
        (dsize >= fsize && (data[fsize] & 0x83) == 0)) {
      amrparse->sync = TRUE;
      *framesize = fsize;
      return TRUE;
    }
  }

  GST_LOG ("sync lost");
  amrparse->sync = FALSE;
  return FALSE;
}


/**
 * gst_amrparse_parse_frame:
 * @parse: #GstBaseParse.
 * @buffer: #GstBuffer.
 *
 * Implementation of "parse" vmethod in #GstBaseParse class.
 *
 * Returns: #GstFlowReturn defining the parsing status.
 */
GstFlowReturn
gst_amrparse_parse_frame (GstBaseParse * parse, GstBuffer * buffer)
{
  GstAmrParse *amrparse;

  amrparse = GST_AMRPARSE (parse);

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gint64 btime;
    gboolean r = gst_amrparse_convert (parse, GST_FORMAT_BYTES,
        GST_BUFFER_OFFSET (buffer),
        GST_FORMAT_TIME, &btime);
    if (r) {
      /* FIXME: What to do if the conversion fails? */
      amrparse->ts = btime;
    }
  }

  GST_BUFFER_DURATION (buffer) = AMR_FRAME_DURATION;
  GST_BUFFER_TIMESTAMP (buffer) = amrparse->ts;

  if (GST_CLOCK_TIME_IS_VALID (amrparse->ts)) {
    amrparse->ts += AMR_FRAME_DURATION;
  }

  if (!(++amrparse->framecount % 50)) {
    gst_amrparse_update_duration (amrparse);
  }
  amrparse->bytecount += GST_BUFFER_SIZE (buffer);

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (parse->srcpad));
  return GST_FLOW_OK;
}


/**
 * gst_amrparse_start:
 * @parse: #GstBaseParse.
 *
 * Implementation of "start" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_amrparse_start (GstBaseParse * parse)
{
  GstAmrParse *amrparse;

  amrparse = GST_AMRPARSE (parse);
  GST_DEBUG ("start");
  amrparse->need_header = TRUE;
  amrparse->header = 0;
  amrparse->sync = TRUE;
  amrparse->eos = FALSE;
  amrparse->framecount = 0;
  amrparse->bytecount = 0;
  amrparse->ts = 0;
  return TRUE;
}


/**
 * gst_amrparse_stop:
 * @parse: #GstBaseParse.
 *
 * Implementation of "stop" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_amrparse_stop (GstBaseParse * parse)
{
  GstAmrParse *amrparse;

  amrparse = GST_AMRPARSE (parse);
  GST_DEBUG ("stop");
  amrparse->need_header = TRUE;
  amrparse->header = 0;
  amrparse->ts = -1;
  return TRUE;
}


/**
 * gst_amrparse_event:
 * @parse: #GstBaseParse.
 * @event: #GstEvent.
 *
 * Implementation of "event" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if the event was handled and can be dropped.
 */
gboolean
gst_amrparse_event (GstBaseParse * parse, GstEvent * event)
{
  GstAmrParse *amrparse;

  amrparse = GST_AMRPARSE (parse);
  GST_DEBUG ("event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      amrparse->eos = TRUE;
      GST_DEBUG ("EOS event");
      break;
    default:
      break;
  }

  return parent_class->event (parse, event);
}


/**
 * gst_amrparse_convert:
 * @parse: #GstBaseParse.
 * @src_format: #GstFormat describing the source format.
 * @src_value: Source value to be converted.
 * @dest_format: #GstFormat defining the converted format.
 * @dest_value: Pointer where the conversion result will be put.
 *
 * Implementation of "convert" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if conversion was successful.
 */
gboolean
gst_amrparse_convert (GstBaseParse * parse,
    GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;
  GstAmrParse *amrparse;
  gfloat bpf;

  amrparse = GST_AMRPARSE (parse);

  /* We are not able to do any estimations until some data has been passed */
  if (!amrparse->framecount)
    return FALSE;

  bpf = (gfloat) amrparse->bytecount / amrparse->framecount;

  if (src_format == GST_FORMAT_BYTES) {
    if (dest_format == GST_FORMAT_TIME) {
      /* BYTES -> TIME conversion */
      GST_DEBUG ("converting bytes -> time");

      if (amrparse->framecount) {
        *dest_value = AMR_FRAME_DURATION * (src_value - amrparse->header) / bpf;
        GST_DEBUG ("conversion result: %" G_GINT64_FORMAT " ms",
            *dest_value / GST_MSECOND);
        ret = TRUE;
      }
    }
  } else if (src_format == GST_FORMAT_TIME) {
    GST_DEBUG ("converting time -> bytes");
    if (dest_format == GST_FORMAT_BYTES) {
      if (amrparse->framecount) {
        *dest_value = bpf * src_value / AMR_FRAME_DURATION + amrparse->header;
        GST_DEBUG ("time %" G_GINT64_FORMAT " ms in bytes = %" G_GINT64_FORMAT,
            src_value / GST_MSECOND, *dest_value);
        ret = TRUE;
      }
    }
  } else if (src_format == GST_FORMAT_DEFAULT) {
    /* DEFAULT == frame-based */
    if (dest_format == GST_FORMAT_TIME) {
      *dest_value = src_value * AMR_FRAME_DURATION;
      ret = TRUE;
    } else if (dest_format == GST_FORMAT_BYTES) {
    }
  }

  return ret;
}
