/* GStreamer TTA plugin
 * (c) 2004 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * based on ttalib
 * (c) 1999-2004 Alexander Djourik <sasha@iszf.irk.ru>
 * 
 * gstttadec.c: raw TTA bitstream decoder
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>

#include <math.h>
#include <string.h>

#include "gstttadec.h"
#include "ttadec.h"
#include "filters.h"

#define TTA_BUFFER_SIZE (1024 * 32 * 8)

/* this is from ttadec.h originally */

static const unsigned long bit_mask[] = {
  0x00000000, 0x00000001, 0x00000003, 0x00000007,
  0x0000000f, 0x0000001f, 0x0000003f, 0x0000007f,
  0x000000ff, 0x000001ff, 0x000003ff, 0x000007ff,
  0x00000fff, 0x00001fff, 0x00003fff, 0x00007fff,
  0x0000ffff, 0x0001ffff, 0x0003ffff, 0x0007ffff,
  0x000fffff, 0x001fffff, 0x003fffff, 0x007fffff,
  0x00ffffff, 0x01ffffff, 0x03ffffff, 0x07ffffff,
  0x0fffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff,
  0xffffffff
};

static const unsigned long bit_shift[] = {
  0x00000001, 0x00000002, 0x00000004, 0x00000008,
  0x00000010, 0x00000020, 0x00000040, 0x00000080,
  0x00000100, 0x00000200, 0x00000400, 0x00000800,
  0x00001000, 0x00002000, 0x00004000, 0x00008000,
  0x00010000, 0x00020000, 0x00040000, 0x00080000,
  0x00100000, 0x00200000, 0x00400000, 0x00800000,
  0x01000000, 0x02000000, 0x04000000, 0x08000000,
  0x10000000, 0x20000000, 0x40000000, 0x80000000,
  0x80000000, 0x80000000, 0x80000000, 0x80000000,
  0x80000000, 0x80000000, 0x80000000, 0x80000000
};

static const unsigned long *shift_16 = bit_shift + 4;

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tta, "
        "width = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 8000, 96000 ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) { 8, 16, 24 }, "
        "depth = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 8000, 96000 ], "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true")
    );

static void gst_tta_dec_class_init (GstTtaDecClass * klass);
static void gst_tta_dec_base_init (GstTtaDecClass * klass);
static void gst_tta_dec_init (GstTtaDec * ttadec);

static GstFlowReturn gst_tta_dec_chain (GstPad * pad, GstBuffer * in);

static GstElementClass *parent = NULL;

static gboolean
gst_tta_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstTtaDec *ttadec = GST_TTA_DEC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstCaps *srccaps;
  gint bits, channels;
  gint32 samplerate;

//  if (!gst_caps_is_fixed (caps))
//    return GST_PAD_LINK_DELAYED;

  gst_structure_get_int (structure, "rate", &samplerate);
  ttadec->samplerate = (guint32) samplerate;
  gst_structure_get_int (structure, "channels", &channels);
  ttadec->channels = (guint) channels;
  gst_structure_get_int (structure, "width", &bits);
  ttadec->bytes = bits / 8;

  srccaps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, ttadec->samplerate,
      "channels", G_TYPE_INT, ttadec->channels,
      "depth", G_TYPE_INT, bits,
      "width", G_TYPE_INT, bits,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (!gst_pad_set_caps (ttadec->srcpad, srccaps))
    return FALSE;

  ttadec->frame_length = FRAME_TIME * ttadec->samplerate;

  ttadec->tta = g_malloc (ttadec->channels * sizeof (decoder));
  ttadec->cache = g_malloc (ttadec->channels * sizeof (long));

  ttadec->decdata =
      (guchar *) g_malloc (ttadec->channels * ttadec->frame_length *
      ttadec->bytes * sizeof (guchar));

  return TRUE;
}

GType
gst_tta_dec_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstTtaDecClass),
      (GBaseInitFunc) gst_tta_dec_base_init,
      NULL,
      (GClassInitFunc) gst_tta_dec_class_init,
      NULL,
      NULL,
      sizeof (GstTtaDec),
      0,
      (GInstanceInitFunc) gst_tta_dec_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstTtaDec", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_tta_dec_base_init (GstTtaDecClass * klass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_static_metadata (element_class, "TTA audio decoder",
      "Codec/Decoder/Audio",
      "Decode TTA audio data", "Arwed v. Merkatz <v.merkatz@gmx.net>");
}

static void
gst_tta_dec_dispose (GObject * object)
{
  GstTtaDec *ttadec = GST_TTA_DEC (object);

  g_free (ttadec->tta);
  g_free (ttadec->decdata);
  g_free (ttadec->tta_buf.buffer);

  G_OBJECT_CLASS (parent)->dispose (object);
}

static void
gst_tta_dec_class_init (GstTtaDecClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_tta_dec_dispose;
}

static void
gst_tta_dec_init (GstTtaDec * ttadec)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ttadec);

  ttadec->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (ttadec->sinkpad, gst_tta_dec_setcaps);

  ttadec->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_fixed_caps (ttadec->srcpad);

  gst_element_add_pad (GST_ELEMENT (ttadec), ttadec->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ttadec), ttadec->srcpad);
  gst_pad_set_chain_function (ttadec->sinkpad, gst_tta_dec_chain);
  ttadec->tta_buf.buffer = (guchar *) g_malloc (TTA_BUFFER_SIZE + 4);
  ttadec->tta_buf.buffer_end = ttadec->tta_buf.buffer + TTA_BUFFER_SIZE;
}

static void
rice_init (adapt * rice, unsigned long k0, unsigned long k1)
{
  rice->k0 = k0;
  rice->k1 = k1;
  rice->sum0 = shift_16[k0];
  rice->sum1 = shift_16[k1];
}

static void
decoder_init (decoder * tta, long nch, long byte_size)
{
  long shift = flt_set[byte_size - 1];
  long i;

  for (i = 0; i < nch; i++) {
    filter_init (&tta[i].fst, shift);
    rice_init (&tta[i].rice, 10, 10);
    tta[i].last = 0;
  }
}

static void
get_binary (tta_buffer * tta_buf, guchar * buffer, unsigned long buffersize,
    unsigned long *value, unsigned long bits)
{
  while (tta_buf->bit_count < bits) {
    if (tta_buf->bitpos == tta_buf->buffer_end) {
      int max =
          TTA_BUFFER_SIZE <=
          buffersize - tta_buf->offset ? TTA_BUFFER_SIZE : buffersize -
          tta_buf->offset;
      memcpy (tta_buf->buffer, buffer + tta_buf->offset, max);
      tta_buf->offset += max;
      tta_buf->bitpos = tta_buf->buffer;
    }

    tta_buf->bit_cache |= *tta_buf->bitpos << tta_buf->bit_count;
    tta_buf->bit_count += 8;
    tta_buf->bitpos++;
  }

  *value = tta_buf->bit_cache & bit_mask[bits];
  tta_buf->bit_cache >>= bits;
  tta_buf->bit_count -= bits;
  tta_buf->bit_cache &= bit_mask[tta_buf->bit_count];
}

static void
get_unary (tta_buffer * tta_buf, guchar * buffer, unsigned long buffersize,
    unsigned long *value)
{
  *value = 0;

  while (!(tta_buf->bit_cache ^ bit_mask[tta_buf->bit_count])) {
    if (tta_buf->bitpos == tta_buf->buffer_end) {
      int max =
          TTA_BUFFER_SIZE <=
          buffersize - tta_buf->offset ? TTA_BUFFER_SIZE : buffersize -
          tta_buf->offset;
      memcpy (tta_buf->buffer, buffer + tta_buf->offset, max);
      tta_buf->offset += max;
      tta_buf->bitpos = tta_buf->buffer;
    }

    *value += tta_buf->bit_count;
    tta_buf->bit_cache = *tta_buf->bitpos++;
    tta_buf->bit_count = 8;
  }

  while (tta_buf->bit_cache & 1) {
    (*value)++;
    tta_buf->bit_cache >>= 1;
    tta_buf->bit_count--;
  }

  tta_buf->bit_cache >>= 1;
  tta_buf->bit_count--;
}

static GstFlowReturn
gst_tta_dec_chain (GstPad * pad, GstBuffer * in)
{
  GstTtaDec *ttadec;
  GstBuffer *outbuf, *buf = GST_BUFFER (in);
  guchar *data, *p;
  decoder *dec;
  unsigned long outsize;
  unsigned long size;
  guint32 frame_samples;
  long res;
  long *prev;

  ttadec = GST_TTA_DEC (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  ttadec->tta_buf.bit_count = 0;
  ttadec->tta_buf.bit_cache = 0;
  ttadec->tta_buf.bitpos = ttadec->tta_buf.buffer_end;
  ttadec->tta_buf.offset = 0;
  decoder_init (ttadec->tta, ttadec->channels, ttadec->bytes);

  if (GST_BUFFER_DURATION_IS_VALID (buf)) {
    frame_samples =
        ceil ((gdouble) (GST_BUFFER_DURATION (buf) * ttadec->samplerate) /
        (gdouble) GST_SECOND);
  } else {
    frame_samples = ttadec->samplerate * FRAME_TIME;
  }
  outsize = ttadec->channels * frame_samples * ttadec->bytes;

  dec = ttadec->tta;
  p = ttadec->decdata;
  prev = ttadec->cache;
  for (res = 0;
      p < ttadec->decdata + frame_samples * ttadec->channels * ttadec->bytes;) {
    unsigned long unary, binary, depth, k;
    long value, temp_value;
    fltst *fst = &dec->fst;
    adapt *rice = &dec->rice;
    long *last = &dec->last;

    // decode Rice unsigned
    get_unary (&ttadec->tta_buf, data, size, &unary);

    switch (unary) {
      case 0:
        depth = 0;
        k = rice->k0;
        break;
      default:
        depth = 1;
        k = rice->k1;
        unary--;
    }

    if (k) {
      get_binary (&ttadec->tta_buf, data, size, &binary, k);
      value = (unary << k) + binary;
    } else
      value = unary;

    switch (depth) {
      case 1:
        rice->sum1 += value - (rice->sum1 >> 4);
        if (rice->k1 > 0 && rice->sum1 < shift_16[rice->k1])
          rice->k1--;
        else if (rice->sum1 > shift_16[rice->k1 + 1])
          rice->k1++;
        value += bit_shift[rice->k0];
      default:
        rice->sum0 += value - (rice->sum0 >> 4);
        if (rice->k0 > 0 && rice->sum0 < shift_16[rice->k0])
          rice->k0--;
        else if (rice->sum0 > shift_16[rice->k0 + 1])
          rice->k0++;
    }

    /* this only uses a temporary variable to silence a gcc warning */
    temp_value = DEC (value);
    value = temp_value;

    // decompress stage 1: adaptive hybrid filter
    hybrid_filter (fst, &value);

    // decompress stage 2: fixed order 1 prediction
    switch (ttadec->bytes) {
      case 1:
        value += PREDICTOR1 (*last, 4);
        break;                  // bps 8
      case 2:
        value += PREDICTOR1 (*last, 5);
        break;                  // bps 16
      case 3:
        value += PREDICTOR1 (*last, 5);
        break;                  // bps 24
      case 4:
        value += *last;
        break;                  // bps 32
    }
    *last = value;

    if (dec < ttadec->tta + ttadec->channels - 1) {
      *prev++ = value;
      dec++;
    } else {
      *prev = value;
      if (ttadec->channels > 1) {
        long *r = prev - 1;

        for (*prev += *r / 2; r >= ttadec->cache; r--)
          *r = *(r + 1) - *r;
        for (r = ttadec->cache; r < prev; r++)
          WRITE_BUFFER (r, ttadec->bytes, p);
      }
      WRITE_BUFFER (prev, ttadec->bytes, p);
      prev = ttadec->cache;
      res++;
      dec = ttadec->tta;
    }
  }

  outbuf = gst_buffer_new_and_alloc (outsize);
  memcpy (GST_BUFFER_DATA (outbuf), ttadec->decdata, outsize);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ttadec->srcpad));
  return gst_pad_push (ttadec->srcpad, outbuf);
}

gboolean
gst_tta_dec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "ttadec",
      GST_RANK_NONE, GST_TYPE_TTA_DEC);
}
