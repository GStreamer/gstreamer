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


#define PCM_BUFFER_SIZE         (1152*4)

/*#define DEBUG_ENABLED*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstac3parse.h"

/* struct and table stolen from ac3dec by Aaron Holtzman */
struct frmsize_s
{
  guint16 bit_rate;
  guint16 frm_size[3];
};

static struct frmsize_s frmsizecod_tbl[] = {
  {32, {64, 69, 96}},
  {32, {64, 70, 96}},
  {40, {80, 87, 120}},
  {40, {80, 88, 120}},
  {48, {96, 104, 144}},
  {48, {96, 105, 144}},
  {56, {112, 121, 168}},
  {56, {112, 122, 168}},
  {64, {128, 139, 192}},
  {64, {128, 140, 192}},
  {80, {160, 174, 240}},
  {80, {160, 175, 240}},
  {96, {192, 208, 288}},
  {96, {192, 209, 288}},
  {112, {224, 243, 336}},
  {112, {224, 244, 336}},
  {128, {256, 278, 384}},
  {128, {256, 279, 384}},
  {160, {320, 348, 480}},
  {160, {320, 349, 480}},
  {192, {384, 417, 576}},
  {192, {384, 418, 576}},
  {224, {448, 487, 672}},
  {224, {448, 488, 672}},
  {256, {512, 557, 768}},
  {256, {512, 558, 768}},
  {320, {640, 696, 960}},
  {320, {640, 697, 960}},
  {384, {768, 835, 1152}},
  {384, {768, 836, 1152}},
  {448, {896, 975, 1344}},
  {448, {896, 976, 1344}},
  {512, {1024, 1114, 1536}},
  {512, {1024, 1115, 1536}},
  {576, {1152, 1253, 1728}},
  {576, {1152, 1254, 1728}},
  {640, {1280, 1393, 1920}},
  {640, {1280, 1394, 1920}}
};

/* GstAc3Parse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SKIP
      /* FILL ME */
};

static GstStaticPadTemplate gst_ac3parse_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3, "
        "channels = (int) [ 1, 6 ], " "rate = (int) [ 32000, 48000 ]")
    );

static GstStaticPadTemplate gst_ac3parse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3")
    );

static void gst_ac3parse_class_init (gpointer g_class);
static void gst_ac3parse_init (GstAc3Parse * ac3parse);

static void gst_ac3parse_chain (GstPad * pad, GstData * data);

static void gst_ac3parse_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ac3parse_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_ac3parse_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_ac3parse_signals[LAST_SIGNAL] = { 0 };*/

GType
ac3parse_get_type (void)
{
  static GType ac3parse_type = 0;

  if (!ac3parse_type) {
    static const GTypeInfo ac3parse_info = {
      sizeof (GstAc3ParseClass), NULL,
      NULL,
      (GClassInitFunc) gst_ac3parse_class_init,
      NULL,
      NULL,
      sizeof (GstAc3Parse),
      0,
      (GInstanceInitFunc) gst_ac3parse_init,
    };

    ac3parse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAc3Parse", &ac3parse_info,
        0);
  }
  return ac3parse_type;
}

static void
gst_ac3parse_class_init (gpointer g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAc3ParseClass *klass;

  klass = (GstAc3ParseClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_ac3parse_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_ac3parse_sink_template));
  gst_element_class_set_details_simple (gstelement_class, "AC3 Parser",
      "Codec/Parser/Audio",
      "Parses and frames AC3 audio streams, provides seek",
      "Erik Walthinsen <omega@cse.ogi.edu>");

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SKIP, g_param_spec_int ("skip", "skip", "skip", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));      /* CHECKME */

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ac3parse_set_property;
  gobject_class->get_property = gst_ac3parse_get_property;

  gstelement_class->change_state = gst_ac3parse_change_state;
}

static void
gst_ac3parse_init (GstAc3Parse * ac3parse)
{
  ac3parse->sinkpad =
      gst_pad_new_from_static_template (&gst_ac3parse_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (ac3parse), ac3parse->sinkpad);
  gst_pad_set_chain_function (ac3parse->sinkpad, gst_ac3parse_chain);

  ac3parse->srcpad =
      gst_pad_new_from_static_template (&gst_ac3parse_src_template, "src");
  gst_pad_use_explicit_caps (ac3parse->srcpad);
  gst_element_add_pad (GST_ELEMENT (ac3parse), ac3parse->srcpad);

  ac3parse->partialbuf = NULL;
  ac3parse->skip = 0;

  ac3parse->sample_rate = ac3parse->channels = -1;
}

static void
gst_ac3parse_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstAc3Parse *ac3parse;
  guchar *data;
  glong size, offset = 0;
  guint16 header;
  guint8 channeldata, acmod, mask;
  GstBuffer *outbuf = NULL;
  gint bpf;
  guint sample_rate = -1, channels = -1;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
/*  g_return_if_fail(GST_IS_BUFFER(buf)); */

  ac3parse = GST_AC3PARSE (GST_OBJECT_PARENT (pad));
  GST_DEBUG ("ac3parse: received buffer of %d bytes", GST_BUFFER_SIZE (buf));

  /* deal with partial frame from previous buffer */
  if (ac3parse->partialbuf) {
    GstBuffer *merge;

    merge = gst_buffer_merge (ac3parse->partialbuf, buf);
    gst_buffer_unref (buf);
    gst_buffer_unref (ac3parse->partialbuf);
    ac3parse->partialbuf = merge;
  } else {
    ac3parse->partialbuf = buf;
  }

  data = GST_BUFFER_DATA (ac3parse->partialbuf);
  size = GST_BUFFER_SIZE (ac3parse->partialbuf);

  /* we're searching for at least 7 bytes. first the
   * syncinfo, where 2 bytes are for the syncword
   * (header ID, 0x0b77), 2 bytes crc1 (checksum) and 1 byte
   * fscod+fmrsizecod (framerate/bitrate) and then the
   * bitstreaminfo bsid (version), bsmod (data type) and
   * acmod (channel info, 3 bits). Then some "maybe"
   * bits, and then the LFE indicator (subwoofer bit) */
  while (offset < size - 6) {
    int skipped = 0;

    GST_DEBUG ("ac3parse: offset %ld, size %ld ", offset, size);

    /* search for a possible start byte */
    for (; ((data[offset] != 0x0b) && (offset < size - 6)); offset++)
      skipped++;
    if (skipped) {
      fprintf (stderr, "ac3parse: **** now at %ld skipped %d bytes (FIXME?)\n",
          offset, skipped);
    }
    /* construct the header word */
    header = GST_READ_UINT16_BE (data + offset);
/*    g_print("AC3PARSE: sync word is 0x%02X\n",header); */
    /* if it's a valid header, go ahead and send off the frame */
    if (header == 0x0b77) {
      gint rate, fsize;

/*      g_print("AC3PARSE: found sync at %d\n",offset); */
      /* get the bits we're interested in */
      rate = (data[offset + 4] >> 6) & 0x3;
      switch (rate) {
        case 0x0:              /* 00b */
          sample_rate = 48000;
          break;
        case 0x1:              /* 01b */
          sample_rate = 44100;
          break;
        case 0x2:              /* 10b */
          sample_rate = 32000;
          break;
        case 0x3:              /* 11b */
        default:
          /* reserved. if this happens, we're screwed */
          g_assert (0);
          break;
      }
      fsize = data[offset + 4] & 0x3f;
      /* calculate the bpf of the frame */
      bpf = frmsizecod_tbl[fsize].frm_size[rate] * 2;
      /* calculate number of channels */
      channeldata = data[offset + 6];   /* skip bsid/bsmod */
      acmod = (channeldata >> 5) & 0x7;
      switch (acmod) {
        case 0x1:              /* 001b = 1 channel */
          channels = 1;
          break;
        case 0x0:              /* 000b = 2 independent channels */
        case 0x2:              /* 010b = 2x front (stereo) */
          channels = 2;
          break;
        case 0x3:              /* 011b = 3 front */
        case 0x4:              /* 100b = 2 front, 1 rear */
          channels = 3;
          break;
        case 0x5:              /* 101b = 3 front, 1 rear */
        case 0x6:              /* 110b = 2 front, 2 rear */
          channels = 4;
          break;
        case 0x7:              /* 111b = 3 front, 2 rear */
          channels = 5;
          break;
        default:
          /* whaaaaaaaaaaaaaa!!!!!!!!!!! */
          g_assert (0);
      }
      /* fetch LFE bit (subwoofer) */
      mask = 0x10;
      if (acmod & 0x1 && acmod != 0x1)  /* 3 front speakers? */
        mask >>= 2;
      if (acmod & 0x4)          /* surround channel? */
        mask >>= 2;
      if (acmod == 0x2)         /* 2/0 mode? */
        mask >>= 2;
      if (channeldata & mask)   /* LFE: do we have a subwoofer channel? */
        channels++;
      /* if we don't have the whole frame... */
      if ((size - offset) < bpf) {
        GST_DEBUG ("ac3parse: partial buffer needed %ld < %d ", size - offset,
            bpf);
        break;
      } else {
        gboolean need_capsnego = FALSE;

        outbuf = gst_buffer_create_sub (ac3parse->partialbuf, offset, bpf);

        /* make sure our properties still match */
        if (channels > 0 && ac3parse->channels != channels) {
          ac3parse->channels = channels;
          need_capsnego = TRUE;
        }
        if (sample_rate > 0 && ac3parse->sample_rate != sample_rate) {
          ac3parse->sample_rate = sample_rate;
          need_capsnego = TRUE;
        }
        if (need_capsnego) {
          GstCaps *newcaps;

          newcaps = gst_caps_new_simple ("audio/x-ac3",
              "channels", G_TYPE_INT, channels,
              "rate", G_TYPE_INT, sample_rate, NULL);
          gst_pad_set_explicit_caps (ac3parse->srcpad, newcaps);
        }

        offset += bpf;
        if (ac3parse->skip == 0 && GST_PAD_IS_LINKED (ac3parse->srcpad)) {
          GST_DEBUG ("ac3parse: pushing buffer of %d bytes",
              GST_BUFFER_SIZE (outbuf));
          gst_pad_push (ac3parse->srcpad, GST_DATA (outbuf));
        } else {
          GST_DEBUG ("ac3parse: skipping buffer of %d bytes",
              GST_BUFFER_SIZE (outbuf));
          gst_buffer_unref (outbuf);
          ac3parse->skip--;
        }
      }
    } else {
      offset++;
      fprintf (stderr, "ac3parse: *** wrong header, skipping byte (FIXME?)\n");
    }
  }
  /* if we have processed this block and there are still */
  /* bytes left not in a partial block, copy them over. */
  if (size - offset > 0) {
    gint remainder = (size - offset);

    GST_DEBUG ("ac3parse: partial buffer needed %d for trailing bytes",
        remainder);

    outbuf = gst_buffer_create_sub (ac3parse->partialbuf, offset, remainder);
    gst_buffer_unref (ac3parse->partialbuf);
    ac3parse->partialbuf = outbuf;
  }
}

static void
gst_ac3parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAc3Parse *src;

  g_return_if_fail (GST_IS_AC3PARSE (object));
  src = GST_AC3PARSE (object);

  switch (prop_id) {
    case ARG_SKIP:
      src->skip = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_ac3parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAc3Parse *src;

  g_return_if_fail (GST_IS_AC3PARSE (object));
  src = GST_AC3PARSE (object);

  switch (prop_id) {
    case ARG_SKIP:
      g_value_set_int (value, src->skip);
      break;
    default:
      break;
  }
}

static GstStateChangeReturn
gst_ac3parse_change_state (GstElement * element, GstStateChange transition)
{
  GstAc3Parse *ac3parse = GST_AC3PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* reset stream info */
      ac3parse->channels = ac3parse->sample_rate = -1;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "ac3parse", GST_RANK_NONE,
          GST_TYPE_AC3PARSE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ac3parse",
    "ac3 parsing", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
