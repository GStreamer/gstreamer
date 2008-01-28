/* GStreamer h264 parser
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
 *
 * gsth264parse.c:
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


#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsth264parse.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

GST_DEBUG_CATEGORY_STATIC (h264_parse_debug);
#define GST_CAT_DEFAULT h264_parse_debug

static const GstElementDetails gst_h264_parse_details =
GST_ELEMENT_DETAILS ("H264Parse",
    "Codec/Parser",
    "Parses raw h264 stream",
    "Michal Benes <michal.benes@itonis.tv>,"
    "Wim Taymans <wim.taymans@gmail.com");

#define DEFAULT_SPLIT_PACKETIZED     FALSE

enum
{
  PROP_0,
  PROP_SPLIT_PACKETIZED
};

typedef enum
{
  NAL_UNKNOWN = 0,
  NAL_SLICE = 1,
  NAL_SLICE_DPA = 2,
  NAL_SLICE_DPB = 3,
  NAL_SLICE_DPC = 4,
  NAL_SLICE_IDR = 5,
  NAL_SEI = 6,
  NAL_SPS = 7,
  NAL_PPS = 8,
  NAL_AU_DELIMITER = 9,
  NAL_SEQ_END = 10,
  NAL_STREAM_END = 11,
  NAL_FILTER_DATA = 12
} GstNalUnitType;

/* small linked list implementation to allocate the list entry and the data in
 * one go */
struct _GstNalList
{
  GstNalList *next;

  gint nal_type;
  gint nal_ref_idc;
  gint first_mb_in_slice;
  gint slice_type;
  gboolean slice;
  gboolean i_frame;

  GstBuffer *buffer;
};

static GstNalList *
gst_nal_list_new (GstBuffer * buffer)
{
  GstNalList *new_list;

  new_list = g_slice_new0 (GstNalList);
  new_list->buffer = buffer;

  return new_list;
}

static GstNalList *
gst_nal_list_prepend_link (GstNalList * list, GstNalList * link)
{
  link->next = list;

  return link;
}

static GstNalList *
gst_nal_list_delete_head (GstNalList * list)
{
  if (list) {
    GstNalList *old = list;

    list = list->next;

    g_slice_free (GstNalList, old);
  }
  return list;
}

/* simple bitstream parser, automatically skips over
 * emulation_prevention_three_bytes. */
typedef struct
{
  guint8 *data;
  guint8 *end;
  gint head;                    /* bitpos in the cache of next bit */
  guint64 cache;                /* cached bytes */
} GstNalBs;

static void
gst_nal_bs_init (GstNalBs * bs, guint8 * data, guint size)
{
  bs->data = data;
  bs->end = data + size;
  bs->head = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  bs->cache = 0xffffffff;
}

static guint32
gst_nal_bs_read (GstNalBs * bs, guint n)
{
  guint32 res = 0;
  gint shift;

  if (n == 0)
    return res;

  /* fill up the cache if we need to */
  while (bs->head < n) {
    guint8 byte;
    gboolean check_three_byte;

    check_three_byte = TRUE;
  next_byte:
    if (bs->data >= bs->end) {
      /* we're at the end, can't produce more than head number of bits */
      n = bs->head;
      break;
    }
    /* get the byte, this can be an emulation_prevention_three_byte that we need
     * to ignore. */
    byte = *bs->data++;
    if (check_three_byte && byte == 0x03 && ((bs->cache & 0xffff) == 0)) {
      /* next byte goes unconditionally to the cache, even if it's 0x03 */
      check_three_byte = FALSE;
      goto next_byte;
    }
    /* shift bytes in cache, moving the head bits of the cache left */
    bs->cache = (bs->cache << 8) | byte;
    bs->head += 8;
  }

  /* bring the required bits down and truncate */
  if ((shift = bs->head - n) > 0)
    res = bs->cache >> shift;
  else
    res = bs->cache;

  /* mask out required bits */
  if (n < 32)
    res &= (1 << n) - 1;

  bs->head = shift;

  return res;
}

static gboolean
gst_nal_bs_eos (GstNalBs * bs)
{
  return (bs->data >= bs->end) && (bs->head == 0);
}

/* read unsigned Exp-Golomb code */
static gint
gst_nal_bs_read_ue (GstNalBs * bs)
{
  gint i = 0;

  while (gst_nal_bs_read (bs, 1) == 0 && !gst_nal_bs_eos (bs) && i < 32)
    i++;

  return ((1 << i) - 1 + gst_nal_bs_read (bs, i));
}


GST_BOILERPLATE (GstH264Parse, gst_h264_parse, GstElement, GST_TYPE_ELEMENT);

static void gst_h264_parse_finalize (GObject * object);
static void gst_h264_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h264_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_h264_parse_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_h264_parse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_h264_parse_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstStateChangeReturn gst_h264_parse_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_h264_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_h264_parse_details);

  GST_DEBUG_CATEGORY_INIT (h264_parse_debug, "h264parse", 0, "h264 parser");
}

static void
gst_h264_parse_class_init (GstH264ParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_h264_parse_finalize);
  gobject_class->set_property = gst_h264_parse_set_property;
  gobject_class->get_property = gst_h264_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_SPLIT_PACKETIZED,
      g_param_spec_boolean ("split-packetized", "Split packetized",
          "Split NAL units of packetized streams", DEFAULT_SPLIT_PACKETIZED,
          G_PARAM_READWRITE));

  gstelement_class->change_state = gst_h264_parse_change_state;
}

static void
gst_h264_parse_init (GstH264Parse * h264parse, GstH264ParseClass * g_class)
{
  h264parse->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (h264parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_h264_parse_chain));
  gst_pad_set_event_function (h264parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_h264_parse_sink_event));
  gst_pad_set_setcaps_function (h264parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_h264_parse_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (h264parse), h264parse->sinkpad);

  h264parse->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_element_add_pad (GST_ELEMENT (h264parse), h264parse->srcpad);

  h264parse->split_packetized = DEFAULT_SPLIT_PACKETIZED;
  h264parse->adapter = gst_adapter_new ();
}

static void
gst_h264_parse_finalize (GObject * object)
{
  GstH264Parse *h264parse;

  h264parse = GST_H264PARSE (object);

  g_object_unref (h264parse->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h264_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH264Parse *parse;

  parse = GST_H264PARSE (object);

  switch (prop_id) {
    case PROP_SPLIT_PACKETIZED:
      parse->split_packetized = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h264_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstH264Parse *parse;

  parse = GST_H264PARSE (object);

  switch (prop_id) {
    case PROP_SPLIT_PACKETIZED:
      g_value_set_boolean (value, parse->split_packetized);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_h264_parse_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res;
  GstH264Parse *h264parse;
  GstStructure *str;
  const GValue *value;

  h264parse = GST_H264PARSE (GST_PAD_PARENT (pad));

  str = gst_caps_get_structure (caps, 0);

  /* packetized video has a codec_data */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    GST_DEBUG_OBJECT (h264parse, "have packetized h264");
    h264parse->packetized = TRUE;
    /* FIXME, PPS, SPS have vital info for detecting new I-frames */
  } else {
    GST_DEBUG_OBJECT (h264parse, "have bytestream h264");
    h264parse->packetized = FALSE;
  }

  /* forward the caps */
  res = gst_pad_set_caps (h264parse->srcpad, caps);

  return res;
}

static void
gst_h264_parse_clear_queues (GstH264Parse * h264parse)
{
  g_list_foreach (h264parse->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (h264parse->gather);
  h264parse->gather = NULL;
  while (h264parse->decode) {
    gst_buffer_unref (h264parse->decode->buffer);
    h264parse->decode = gst_nal_list_delete_head (h264parse->decode);
  }
  h264parse->decode = NULL;
  h264parse->decode_len = 0;
  if (h264parse->prev) {
    gst_buffer_unref (h264parse->prev);
    h264parse->prev = NULL;
  }
  gst_adapter_clear (h264parse->adapter);
  h264parse->have_i_frame = FALSE;
}

static GstFlowReturn
gst_h264_parse_chain_forward (GstH264Parse * h264parse, gboolean discont,
    GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  const guint8 *data;

  if (discont) {
    gst_adapter_clear (h264parse->adapter);
    h264parse->discont = TRUE;
  }

  gst_adapter_push (h264parse->adapter, buffer);

  while (res == GST_FLOW_OK) {
    gint i;
    gint next_nalu_pos = -1;
    guint32 nalu_size;
    gint avail;
    gboolean delta_unit = TRUE;

    avail = gst_adapter_available (h264parse->adapter);
    if (avail < 5)
      break;
    data = gst_adapter_peek (h264parse->adapter, avail);

    nalu_size = (data[0] << 24)
        + (data[1] << 16) + (data[2] << 8) + data[3];

    if (nalu_size == 1) {
      /* Bytestream format */
      /* Find next NALU header */
      for (i = 1; i < avail - 4; ++i) {
        if (data[i + 0] == 0 && data[i + 1] == 0 && data[i + 2] == 0
            && data[i + 3] == 1) {
          next_nalu_pos = i;
          break;
        }
      }
    } else {
      /* Packetized format, see if we have to split it, usually splitting is not
       * a good idea as decoders have no way of handling it. */
      if (h264parse->split_packetized) {
        if (nalu_size + 4 <= avail)
          next_nalu_pos = nalu_size + 4;
      } else {
        next_nalu_pos = avail;
      }
    }
    /* Figure out if this is a delta unit */
    {
      gint nal_type, nal_ref_idc;

      nal_type = (data[4] & 0x1f);
      nal_ref_idc = (data[4] & 0x60) >> 5;

      GST_DEBUG_OBJECT (h264parse, "NAL type: %d, ref_idc: %d", nal_type,
          nal_ref_idc);

      /* first parse some things needed to get to the frame type */
      if (nal_type >= NAL_SLICE && nal_type <= NAL_SLICE_IDR) {
        GstNalBs bs;
        gint first_mb_in_slice, slice_type;
        guint8 *bs_data = (guint8 *) data + 5;

        gst_nal_bs_init (&bs, bs_data, avail - 5);

        first_mb_in_slice = gst_nal_bs_read_ue (&bs);
        slice_type = gst_nal_bs_read_ue (&bs);

        GST_DEBUG_OBJECT (h264parse, "first MB: %d, slice type: %d",
            first_mb_in_slice, slice_type);

        switch (slice_type) {
          case 0:
          case 5:
          case 3:
          case 8:              /* SP */
            /* P frames */
            GST_DEBUG_OBJECT (h264parse, "we have a P slice");
            delta_unit = TRUE;
            break;
          case 1:
          case 6:
            /* B frames */
            GST_DEBUG_OBJECT (h264parse, "we have a B slice");
            delta_unit = TRUE;
            break;
          case 2:
          case 7:
          case 4:
          case 9:
            /* I frames */
            GST_DEBUG_OBJECT (h264parse, "we have an I slice");
            delta_unit = FALSE;
            break;
        }
      } else if (nal_type >= NAL_SPS && nal_type <= NAL_PPS) {
        /* This can be considered as a non delta unit */
        GST_DEBUG_OBJECT (h264parse, "we have a SPS or PPS NAL");
        delta_unit = FALSE;
      }
    }

    /* we have a packet */
    if (next_nalu_pos > 0) {
      GstBuffer *outbuf;

      outbuf = gst_adapter_take_buffer (h264parse->adapter, next_nalu_pos);

      GST_DEBUG_OBJECT (h264parse,
          "pushing buffer %p, size %u, ts %" GST_TIME_FORMAT, outbuf,
          next_nalu_pos, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

      if (h264parse->discont) {
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        h264parse->discont = FALSE;
      }

      if (delta_unit) {
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
      }

      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (h264parse->srcpad));
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
      res = gst_pad_push (h264parse->srcpad, outbuf);
    } else {
      /* NALU can not be parsed yet, we wait for more data in the adapter. */
      break;
    }
  }
  return res;
}

static GstFlowReturn
gst_h264_parse_flush_decode (GstH264Parse * h264parse)
{
  GstFlowReturn res = GST_FLOW_OK;
  gboolean first = TRUE;

  while (h264parse->decode) {
    GstNalList *link;
    GstBuffer *buf;

    link = h264parse->decode;
    buf = link->buffer;

    GST_DEBUG_OBJECT (h264parse, "have type: %d, I frame: %d", link->nal_type,
        link->i_frame);

    if (first) {
      /* first buffer has discont */
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      first = FALSE;
    } else {
      /* next buffers are not discont */
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
    }

    if (link->i_frame)
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    GST_DEBUG_OBJECT (h264parse, "pushing buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    res = gst_pad_push (h264parse->srcpad, buf);

    h264parse->decode = gst_nal_list_delete_head (h264parse->decode);
    h264parse->decode_len--;
  }
  /* the i frame is gone now */
  h264parse->have_i_frame = FALSE;

  return res;
}

/* check that the decode queue contains a valid sync code that should be pushed
 * out before adding @buffer to the decode queue */
static GstFlowReturn
gst_h264_parse_queue_buffer (GstH264Parse * parse, GstBuffer * buffer)
{
  guint8 *data;
  guint size;
  guint32 nalu_size;
  GstNalBs bs;
  GstNalList *link;
  GstFlowReturn res = GST_FLOW_OK;
  GstClockTime timestamp;

  /* create new NALU link */
  link = gst_nal_list_new (buffer);

  /* first parse the buffer */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  link->slice = FALSE;
  link->i_frame = FALSE;

  GST_DEBUG_OBJECT (parse,
      "analyse buffer of size %u, timestamp %" GST_TIME_FORMAT, size,
      GST_TIME_ARGS (timestamp));

  /* now parse all the NAL units in this buffer, for bytestream we only have one
   * NAL unit but for packetized streams we can have multiple ones */
  while (size >= 5) {
    nalu_size = (data[0] << 24)
        + (data[1] << 16) + (data[2] << 8) + data[3];

    link->nal_ref_idc = (data[4] & 0x60) >> 5;
    link->nal_type = (data[4] & 0x1f);

    GST_DEBUG_OBJECT (parse, "size: %u, NAL type: %d, ref_idc: %d",
        nalu_size, link->nal_type, link->nal_ref_idc);

    /* first parse some things needed to get to the frame type */
    if (link->nal_type >= NAL_SLICE && link->nal_type <= NAL_SLICE_IDR) {
      gst_nal_bs_init (&bs, data + 5, size - 5);

      link->first_mb_in_slice = gst_nal_bs_read_ue (&bs);
      link->slice_type = gst_nal_bs_read_ue (&bs);
      link->slice = TRUE;

      GST_DEBUG_OBJECT (parse, "first MB: %d, slice type: %d",
          link->first_mb_in_slice, link->slice_type);

      switch (link->slice_type) {
        case 0:
        case 5:
        case 3:
        case 8:                /* SP */
          /* P frames */
          GST_DEBUG_OBJECT (parse, "we have a P slice");
          break;
        case 1:
        case 6:
          /* B frames */
          GST_DEBUG_OBJECT (parse, "we have a B slice");
          break;
        case 2:
        case 7:
        case 4:
        case 9:
          /* I frames */
          GST_DEBUG_OBJECT (parse, "we have an I slice");
          link->i_frame = TRUE;
          break;
      }
    }
    /* bytestream, we can exit now */
    if (nalu_size == 1)
      break;

    /* packetized format, continue parsing all packets, skip size */
    nalu_size += 4;
    data += nalu_size;
    size -= nalu_size;
  }

  /* we have an I frame in the queue, this new NAL unit is a slice but not
   * an I frame, output the decode queue */
  GST_DEBUG_OBJECT (parse, "have_I_frame: %d, I_frame: %d, slice: %d",
      parse->have_i_frame, link->i_frame, link->slice);
  if (parse->have_i_frame && !link->i_frame && link->slice) {
    GST_DEBUG_OBJECT (parse, "flushing decode queue");
    res = gst_h264_parse_flush_decode (parse);
  }
  if (link->i_frame)
    /* we're going to add a new I-frame in the queue */
    parse->have_i_frame = TRUE;

  parse->decode = gst_nal_list_prepend_link (parse->decode, link);
  parse->decode_len++;
  GST_DEBUG_OBJECT (parse,
      "copied %d bytes of NAL to decode queue. queue size %d", size,
      parse->decode_len);

  return res;
}

static guint
gst_h264_parse_find_start_reverse (GstH264Parse * parse, guint8 * data,
    guint size, guint32 * code)
{
  guint32 search = *code;

  while (size > 0) {
    /* the sync code is kept in reverse */
    search = (search << 8) | (data[size - 1]);
    if (search == 0x01000000)
      break;

    size--;
  }
  *code = search;

  return size - 1;
}

static GstFlowReturn
gst_h264_parse_chain_reverse (GstH264Parse * h264parse, gboolean discont,
    GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (G_UNLIKELY (discont)) {
    guint start, stop, last;
    guint32 code;
    GstBuffer *prev;
    GstClockTime timestamp;

    GST_DEBUG_OBJECT (h264parse,
        "received discont, copy gathered buffers for decoding");

    /* init start code accumulator */
    stop = -1;
    prev = h264parse->prev;
    h264parse->prev = NULL;

    while (h264parse->gather) {
      GstBuffer *gbuf;
      guint8 *data;

      /* get new buffer and init the start code search to the end position */
      gbuf = GST_BUFFER_CAST (h264parse->gather->data);

      /* remove from the gather list, they are in reverse order */
      h264parse->gather =
          g_list_delete_link (h264parse->gather, h264parse->gather);

      if (h264parse->packetized) {
        /* packetized the packets are already split, we can just parse and 
         * store them */
        GST_DEBUG_OBJECT (h264parse, "copied packetized buffer");
        res = gst_h264_parse_queue_buffer (h264parse, gbuf);
      } else {
        /* bytestream, we have to split the NALUs on the sync markers */
        code = 0xffffffff;
        if (prev) {
          /* if we have a previous buffer or a leftover, merge them together
           * now */
          GST_DEBUG_OBJECT (h264parse, "merging previous buffer");
          gbuf = gst_buffer_join (gbuf, prev);
          prev = NULL;
        }

        last = GST_BUFFER_SIZE (gbuf);
        data = GST_BUFFER_DATA (gbuf);
        timestamp = GST_BUFFER_TIMESTAMP (gbuf);

        GST_DEBUG_OBJECT (h264parse,
            "buffer size: %u, timestamp %" GST_TIME_FORMAT, last,
            GST_TIME_ARGS (timestamp));

        while (last > 0) {
          GST_DEBUG_OBJECT (h264parse, "scan from %u", last);
          /* find a start code searching backwards in this buffer */
          start =
              gst_h264_parse_find_start_reverse (h264parse, data, last, &code);
          if (start != -1) {
            GstBuffer *decode;

            GST_DEBUG_OBJECT (h264parse, "found start code at %u", start);

            /* we found a start code, copy everything starting from it to the
             * decode queue. */
            decode = gst_buffer_create_sub (gbuf, start, last - start);

            GST_BUFFER_TIMESTAMP (decode) = timestamp;

            /* see what we have here */
            res = gst_h264_parse_queue_buffer (h264parse, decode);

            last = start;
          } else {
            /* no start code found, keep the buffer and merge with potential next
             * buffer. */
            GST_DEBUG_OBJECT (h264parse, "no start code, keeping buffer to %u",
                last);
            prev = gst_buffer_create_sub (gbuf, 0, last);
            gst_buffer_unref (gbuf);
            break;
          }
        }
      }
    }
    if (prev) {
      GST_DEBUG_OBJECT (h264parse, "keeping buffer");
      h264parse->prev = prev;
    }
  }
  if (buffer) {
    /* add buffer to gather queue */
    GST_DEBUG_OBJECT (h264parse, "gathering buffer %p, size %u", buffer,
        GST_BUFFER_SIZE (buffer));
    h264parse->gather = g_list_prepend (h264parse->gather, buffer);
  }
  return res;
}

static GstFlowReturn
gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstH264Parse *h264parse;
  gboolean discont;
  GstCaps *caps;

  h264parse = GST_H264PARSE (GST_PAD_PARENT (pad));

  if (!GST_PAD_CAPS (h264parse->srcpad)) {
    /* Set default caps if the sink caps were not negotiated, this is when we
     * are reading from a file or so */
    caps = gst_caps_new_simple ("video/x-h264", NULL);

    /* Set source caps */
    if (!gst_pad_set_caps (h264parse->srcpad, caps))
      goto caps_failed;

    /* we assume the bytestream format but won't really fail otherwise if the
     * data turns out to be a nicely aligned packetized format (except we don't
     * do the codec_data caps with the PPS and SPS). */
    h264parse->packetized = FALSE;

    gst_caps_unref (caps);
  }

  discont = GST_BUFFER_IS_DISCONT (buffer);

  GST_DEBUG_OBJECT (h264parse, "received buffer of size %u",
      GST_BUFFER_SIZE (buffer));

  if (h264parse->segment.rate > 0.0)
    res = gst_h264_parse_chain_forward (h264parse, discont, buffer);
  else
    res = gst_h264_parse_chain_reverse (h264parse, discont, buffer);

  return res;

  /* ERRORS */
caps_failed:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (h264parse),
        CORE, NEGOTIATION, (NULL), ("failed to set caps"));
    gst_caps_unref (caps);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_h264_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstH264Parse *h264parse;
  gboolean res;

  h264parse = GST_H264PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (h264parse, "received FLUSH stop");
      gst_segment_init (&h264parse->segment, GST_FORMAT_UNDEFINED);
      gst_h264_parse_clear_queues (h264parse);
      res = gst_pad_push_event (h264parse->srcpad, event);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (h264parse, "received EOS");
      if (h264parse->segment.rate < 0.0) {
        gst_h264_parse_chain_reverse (h264parse, TRUE, NULL);
        gst_h264_parse_flush_decode (h264parse);
      }
      res = gst_pad_push_event (h264parse->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, pos;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);

      /* now configure the values */
      gst_segment_set_newsegment_full (&h264parse->segment, update,
          rate, applied_rate, format, start, stop, pos);

      GST_DEBUG_OBJECT (h264parse,
          "Pushing newseg rate %g, applied rate %g, format %d, start %"
          G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT ", pos %" G_GINT64_FORMAT,
          rate, applied_rate, format, start, stop, pos);

      res = gst_pad_push_event (h264parse->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (h264parse->srcpad, event);
      break;

  }
  gst_object_unref (h264parse);

  return res;
}

static GstStateChangeReturn
gst_h264_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstH264Parse *h264parse;
  GstStateChangeReturn ret;

  h264parse = GST_H264PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&h264parse->segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_h264_parse_clear_queues (h264parse);
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "h264parse",
      GST_RANK_NONE, GST_TYPE_H264PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "h264parse",
    "Element parsing raw h264 streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
