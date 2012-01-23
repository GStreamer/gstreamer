/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
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
#include "mpegvideoparse.h"

/* FIXME: there are still some things to do in this element.
 * + Handle Sequence Display Extension to output the display size
 *   rather than the encoded size.
 * + Do all the other stuff (documentation, tests) to get it into
 *   ugly or good.
 * + low priority:
 *   - handle seeking in raw elementary streams
 *   - calculate timestamps for all un-timestamped frames, taking into
 *     account frame re-ordering. Doing this probably requires introducing
 *     an extra end-to-end delay however, so might not be really desirable.
 *   - Collect a list of regions and the sequence headers that apply
 *     to each region so that we properly handle SEQUENCE_END followed
 *     by a new sequence. At the moment, the caps will change if the
 *     sequence changes, but if we then seek to a different spot it might
 *     be wrong. Fortunately almost every stream only has 1 sequence.
 */
GST_DEBUG_CATEGORY (mpv_parse_debug);
#define GST_CAT_DEFAULT mpv_parse_debug

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], "
        "parsed = (boolean) true, "
        "systemstream = (boolean) false, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "pixel-aspect-ratio = (fraction) [ 0/1, MAX ], "
        "framerate = (fraction) [ 0/1, MAX ]")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], "
        "parsed = (boolean) false, " "systemstream = (boolean) false")
    );

/* MpegVideoParse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_mpegvideoparse_class_init (MpegVideoParseClass * klass);
static void gst_mpegvideoparse_base_init (MpegVideoParseClass * klass);
static void gst_mpegvideoparse_init (MpegVideoParse * mpegvideoparse);
static void gst_mpegvideoparse_dispose (GObject * object);

static GstFlowReturn gst_mpegvideoparse_chain (GstPad * pad, GstBuffer * buf);
static gboolean mpv_parse_sink_event (GstPad * pad, GstEvent * event);
static void gst_mpegvideoparse_flush (MpegVideoParse * mpegvideoparse);
static GstStateChangeReturn
gst_mpegvideoparse_change_state (GstElement * element,
    GstStateChange transition);

static void mpv_send_pending_segs (MpegVideoParse * mpegvideoparse);
static void mpv_clear_pending_segs (MpegVideoParse * mpegvideoparse);

static GstElementClass *parent_class = NULL;

/*static guint gst_mpegvideoparse_signals[LAST_SIGNAL] = { 0 }; */

GType
mpegvideoparse_get_type (void)
{
  static GType mpegvideoparse_type = 0;

  if (!mpegvideoparse_type) {
    static const GTypeInfo mpegvideoparse_info = {
      sizeof (MpegVideoParseClass),
      (GBaseInitFunc) gst_mpegvideoparse_base_init,
      NULL,
      (GClassInitFunc) gst_mpegvideoparse_class_init,
      NULL,
      NULL,
      sizeof (MpegVideoParse),
      0,
      (GInstanceInitFunc) gst_mpegvideoparse_init,
    };

    mpegvideoparse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "MpegVideoParse",
        &mpegvideoparse_info, 0);
  }
  return mpegvideoparse_type;
}

static void
gst_mpegvideoparse_base_init (MpegVideoParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);

  gst_element_class_set_details_simple (element_class,
      "MPEG video elementary stream parser",
      "Codec/Parser/Video",
      "Parses and frames MPEG-1 and MPEG-2 elementary video streams",
      "Wim Taymans <wim.taymans@chello.be>, "
      "Jan Schmidt <thaytan@mad.scientist.com>");
}

static void
gst_mpegvideoparse_class_init (MpegVideoParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = (GObjectFinalizeFunc) (gst_mpegvideoparse_dispose);
  gstelement_class->change_state = gst_mpegvideoparse_change_state;
}

static void
mpv_parse_reset (MpegVideoParse * mpegvideoparse)
{
  mpegvideoparse->seq_hdr.mpeg_version = 0;
  mpegvideoparse->seq_hdr.width = mpegvideoparse->seq_hdr.height = -1;
  mpegvideoparse->seq_hdr.fps_n = mpegvideoparse->seq_hdr.par_w = 0;
  mpegvideoparse->seq_hdr.fps_d = mpegvideoparse->seq_hdr.par_h = 1;

  mpv_clear_pending_segs (mpegvideoparse);
}

static void
mpv_send_pending_segs (MpegVideoParse * mpegvideoparse)
{
  while (mpegvideoparse->pending_segs) {
    GstEvent *ev = mpegvideoparse->pending_segs->data;

    gst_pad_push_event (mpegvideoparse->srcpad, ev);

    mpegvideoparse->pending_segs =
        g_list_delete_link (mpegvideoparse->pending_segs,
        mpegvideoparse->pending_segs);
  }
  mpegvideoparse->pending_segs = NULL;
}

static void
mpv_clear_pending_segs (MpegVideoParse * mpegvideoparse)
{
  while (mpegvideoparse->pending_segs) {
    GstEvent *ev = mpegvideoparse->pending_segs->data;
    gst_event_unref (ev);

    mpegvideoparse->pending_segs =
        g_list_delete_link (mpegvideoparse->pending_segs,
        mpegvideoparse->pending_segs);
  }
}

static void
gst_mpegvideoparse_init (MpegVideoParse * mpegvideoparse)
{
  mpegvideoparse->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (mpegvideoparse->sinkpad,
      gst_mpegvideoparse_chain);
  gst_pad_set_event_function (mpegvideoparse->sinkpad, mpv_parse_sink_event);
  gst_element_add_pad (GST_ELEMENT (mpegvideoparse), mpegvideoparse->sinkpad);

  mpegvideoparse->srcpad =
      gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (mpegvideoparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (mpegvideoparse), mpegvideoparse->srcpad);

  mpeg_packetiser_init (&mpegvideoparse->packer);

  mpv_parse_reset (mpegvideoparse);
}

void
gst_mpegvideoparse_dispose (GObject * object)
{
  MpegVideoParse *mpegvideoparse = GST_MPEGVIDEOPARSE (object);

  mpeg_packetiser_free (&mpegvideoparse->packer);
  gst_buffer_replace (&mpegvideoparse->seq_hdr_buf, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
mpegvideoparse_handle_sequence (MpegVideoParse * mpegvideoparse,
    GstBuffer * buf)
{
  MPEGSeqHdr new_hdr;
  guint8 *cur, *end;

  cur = GST_BUFFER_DATA (buf);
  end = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);

  memset (&new_hdr, 0, sizeof (MPEGSeqHdr));

  if (G_UNLIKELY (!mpeg_util_parse_sequence_hdr (&new_hdr, cur, end)))
    return FALSE;

  if (new_hdr.width < 16 || new_hdr.width > 4096 ||
      new_hdr.height < 16 || new_hdr.height > 4096) {
    GST_WARNING_OBJECT (mpegvideoparse, "Width/height out of valid range "
        "[16, 4096]");
    return FALSE;
  }

  if (memcmp (&mpegvideoparse->seq_hdr, &new_hdr, sizeof (MPEGSeqHdr)) != 0) {
    GstCaps *caps;
    GstBuffer *seq_buf;
    /*
     * Profile indication - 1 => High, 2 => Spatially Scalable,
     *                      3 => SNR Scalable, 4 => Main, 5 => Simple
     * 4:2:2 and Multi-view have profile = 0, with the escape bit set to 1
     */
    const gchar *profiles[] = { "high", "spatial", "snr", "main", "simple" };
    /*
     * Level indication - 4 => High, 6 => High-1440, 8 => Main, 10 => Low,
     *                    except in the case of profile = 0
     */
    const gchar *levels[] = { "high", "high-1440", "main", "low" };

    /* Store the entire sequence header + sequence header extension
       for output as codec_data */
    seq_buf = gst_buffer_copy (buf);
    gst_buffer_replace (&mpegvideoparse->seq_hdr_buf, seq_buf);
    gst_buffer_unref (seq_buf);

    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "parsed", G_TYPE_BOOLEAN, TRUE,
        "mpegversion", G_TYPE_INT, new_hdr.mpeg_version,
        "width", G_TYPE_INT, new_hdr.width,
        "height", G_TYPE_INT, new_hdr.height,
        "framerate", GST_TYPE_FRACTION, new_hdr.fps_n, new_hdr.fps_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, new_hdr.par_w, new_hdr.par_h,
        "interlaced", G_TYPE_BOOLEAN, !new_hdr.progressive,
        "codec_data", GST_TYPE_BUFFER, seq_buf, NULL);

    if (new_hdr.mpeg_version == 2) {
      const gchar *profile = NULL, *level = NULL;

      if (new_hdr.profile > 0 && new_hdr.profile < 6)
        profile = profiles[new_hdr.profile - 1];

      if ((new_hdr.level > 3) && (new_hdr.level < 11) &&
          (new_hdr.level % 2 == 0))
        level = levels[(new_hdr.level >> 1) - 1];

      if (new_hdr.profile == 8) {
        /* Non-hierarchical profile */
        switch (new_hdr.level) {
          case 2:
            level = levels[0];
            break;
          case 5:
            level = levels[2];
            profile = "4:2:2";
            break;
          case 10:
            level = levels[0];
            break;
          case 11:
            level = levels[1];
            break;
          case 13:
            level = levels[2];
            break;
          case 14:
            level = levels[3];
            profile = "multiview";
            break;
          default:
            break;
        }
      }

      if (profile)
        gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile, NULL);
      else
        GST_DEBUG ("Invalid profile - %u", new_hdr.profile);

      if (level)
        gst_caps_set_simple (caps, "level", G_TYPE_STRING, level, NULL);
      else
        GST_DEBUG ("Invalid level - %u", new_hdr.level);
    }

    GST_DEBUG ("New mpegvideoparse caps: %" GST_PTR_FORMAT, caps);
    if (!gst_pad_set_caps (mpegvideoparse->srcpad, caps)) {
      gst_caps_unref (caps);
      return FALSE;
    }
    gst_caps_unref (caps);

    if (new_hdr.bitrate > 0) {
      GstTagList *taglist;

      taglist = gst_tag_list_new_full (GST_TAG_BITRATE, new_hdr.bitrate, NULL);
      gst_element_found_tags_for_pad (GST_ELEMENT_CAST (mpegvideoparse),
          mpegvideoparse->srcpad, taglist);
    }

    /* And update the new_hdr into our stored version */
    mpegvideoparse->seq_hdr = new_hdr;
  }

  return TRUE;
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
picture_start_code_name (guint8 psc)
{
  guint i;
  const struct
  {
    guint8 psc;
    const gchar *name;
  } psc_names[] = {
    {
    0x00, "Picture Start"}, {
    0xb0, "Reserved"}, {
    0xb1, "Reserved"}, {
    0xb2, "User Data Start"}, {
    0xb3, "Sequence Header Start"}, {
    0xb4, "Sequence Error"}, {
    0xb5, "Extnsion Start"}, {
    0xb6, "Reserved"}, {
    0xb7, "Sequence End"}, {
    0xb8, "Group Start"}, {
    0xb9, "Program End"}
  };
  if (psc < 0xB0 && psc > 0)
    return "Slice Start";

  for (i = 0; i < G_N_ELEMENTS (psc_names); i++)
    if (psc_names[i].psc == psc)
      return psc_names[i].name;

  return "UNKNOWN";
};

static const gchar *
picture_type_name (guint8 pct)
{
  guint i;
  const struct
  {
    guint8 pct;
    const gchar *name;
  } pct_names[] = {
    {
    0, "Forbidden"}, {
    1, "I Frame"}, {
    2, "P Frame"}, {
    3, "B Frame"}, {
    4, "DC Intra Coded (Shall Not Be Used!)"}
  };

  for (i = 0; i < G_N_ELEMENTS (pct_names); i++)
    if (pct_names[i].pct == pct)
      return pct_names[i].name;

  return "Reserved/Unknown";
}
#endif /* GST_DISABLE_GST_DEBUG */

static gboolean
mpegvideoparse_handle_picture (MpegVideoParse * mpegvideoparse, GstBuffer * buf)
{
  guint8 *cur, *end;
  guint32 sync_word = 0xffffffff;

  cur = GST_BUFFER_DATA (buf);
  end = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);

  cur = mpeg_util_find_start_code (&sync_word, cur, end);
  while (cur != NULL) {
    if (cur[0] == 0 || cur[0] > 0xaf)
      GST_LOG_OBJECT (mpegvideoparse, "Picture Start Code : %s",
          picture_start_code_name (cur[0]));
    /* Cur points at the last byte of the start code */
    if (cur[0] == MPEG_PACKET_PICTURE) {
      guint8 *pic_data = cur - 3;
      MPEGPictureHdr hdr;

      /* pic_data points to the first byte of the sync word now */
      if (!mpeg_util_parse_picture_hdr (&hdr, pic_data, end))
        return FALSE;

      if (hdr.pic_type != MPEG_PICTURE_TYPE_I)
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

      GST_LOG_OBJECT (mpegvideoparse, "Picture type is %s",
          picture_type_name (hdr.pic_type));
      /* FIXME: Can use the picture type and number of fields to track a
       * timestamp */
      break;
    }
    cur = mpeg_util_find_start_code (&sync_word, cur, end);
  }

  return TRUE;
}

#if 0
static guint64
gst_mpegvideoparse_time_code (guchar * gop, MPEGSeqHdr * seq_hdr)
{
  guint32 data = GST_READ_UINT32_BE (gop);
  guint64 seconds;
  guint8 frames;

  seconds = ((data & 0xfc000000) >> 26) * 3600; /* hours */
  seconds += ((data & 0x03f00000) >> 20) * 60;  /* minutes */
  seconds += (data & 0x0007e000) >> 13; /* seconds */

  frames = (data & 0x00001f80) >> 7;

  return seconds * GST_SECOND + gst_util_uint64_scale_int (frames * GST_SECOND,
      seq_hdr->fps_d, seq_hdr->fps_n);
}
#endif

static void
gst_mpegvideoparse_flush (MpegVideoParse * mpegvideoparse)
{
  GST_DEBUG_OBJECT (mpegvideoparse, "mpegvideoparse: flushing");

  mpegvideoparse->next_offset = GST_BUFFER_OFFSET_NONE;

  g_list_foreach (mpegvideoparse->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mpegvideoparse->gather);
  mpegvideoparse->gather = NULL;
  g_list_foreach (mpegvideoparse->decode, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mpegvideoparse->decode);
  mpegvideoparse->decode = NULL;
  mpeg_packetiser_flush (&mpegvideoparse->packer);

  mpv_clear_pending_segs (mpegvideoparse);
}

static GstFlowReturn
mpegvideoparse_drain_avail (MpegVideoParse * mpegvideoparse)
{
  MPEGBlockInfo *cur;
  GstBuffer *buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;

  cur = mpeg_packetiser_get_block (&mpegvideoparse->packer, &buf);
  while ((cur != NULL) && (res == GST_FLOW_OK)) {
    /* Handle the block */
    GST_LOG_OBJECT (mpegvideoparse,
        "Have block of size %u with pack_type %s and flags 0x%02x",
        cur->length, picture_start_code_name (cur->first_pack_type),
        cur->flags);

    if ((cur->flags & MPEG_BLOCK_FLAG_SEQUENCE) && buf != NULL) {
      /* Found a sequence header */
      if (!mpegvideoparse_handle_sequence (mpegvideoparse, buf)) {
        GST_DEBUG_OBJECT (mpegvideoparse,
            "Invalid sequence header. Dropping buffer.");
        gst_buffer_unref (buf);
        buf = NULL;
      }
    } else if (mpegvideoparse->seq_hdr.mpeg_version == 0 && buf) {
      /* Don't start pushing out buffers until we've seen a sequence header */
      GST_DEBUG_OBJECT (mpegvideoparse,
          "No sequence header yet. Dropping buffer of %u bytes",
          GST_BUFFER_SIZE (buf));
      gst_buffer_unref (buf);
      buf = NULL;
    }

    if (buf != NULL) {
      /* If outputting a PICTURE packet, we can calculate the duration
         and possibly the timestamp */
      if (cur->flags & MPEG_BLOCK_FLAG_PICTURE) {
        if (!mpegvideoparse_handle_picture (mpegvideoparse, buf)) {
          /* Corrupted picture. Drop it. */
          GST_DEBUG_OBJECT (mpegvideoparse,
              "Corrupted picture header. Dropping buffer of %u bytes",
              GST_BUFFER_SIZE (buf));
          mpegvideoparse->need_discont = TRUE;
          gst_buffer_unref (buf);
          buf = NULL;
        }
      }
    }

    if (buf != NULL) {
      GST_DEBUG_OBJECT (mpegvideoparse,
          "mpegvideoparse: pushing buffer of %u bytes with ts %"
          GST_TIME_FORMAT, GST_BUFFER_SIZE (buf),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

      gst_buffer_set_caps (buf, GST_PAD_CAPS (mpegvideoparse->srcpad));

      if (mpegvideoparse->need_discont) {
        GST_DEBUG_OBJECT (mpegvideoparse,
            "setting discont flag on outgoing buffer");
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
        mpegvideoparse->need_discont = FALSE;
      }

      mpv_send_pending_segs (mpegvideoparse);

      res = gst_pad_push (mpegvideoparse->srcpad, buf);
      buf = NULL;
    }

    /* Advance to the next data block */
    mpeg_packetiser_next_block (&mpegvideoparse->packer);
    cur = mpeg_packetiser_get_block (&mpegvideoparse->packer, &buf);
  }
  if (buf != NULL)
    gst_buffer_unref (buf);

  return res;
}

static GstFlowReturn
gst_mpegvideoparse_chain_forward (MpegVideoParse * mpegvideoparse,
    gboolean discont, GstBuffer * buf)
{
  GstFlowReturn res;
  guint64 next_offset = GST_BUFFER_OFFSET_NONE;

  GST_DEBUG_OBJECT (mpegvideoparse,
      "mpegvideoparse: received buffer of %u bytes with ts %"
      GST_TIME_FORMAT " and offset %" G_GINT64_FORMAT, GST_BUFFER_SIZE (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_OFFSET (buf));

  /* If we have an offset, and the incoming offset doesn't match, 
     or we have a discont, handle it first by flushing out data
     we have collected. */
  if (mpegvideoparse->next_offset != GST_BUFFER_OFFSET_NONE) {
    if (GST_BUFFER_OFFSET_IS_VALID (buf)) {
      if (mpegvideoparse->next_offset != GST_BUFFER_OFFSET (buf))
        discont = TRUE;
      next_offset = GST_BUFFER_OFFSET (buf) + GST_BUFFER_SIZE (buf);
    } else {
      next_offset = mpegvideoparse->next_offset + GST_BUFFER_SIZE (buf);
    }
  }

  /* Clear out any existing stuff if the new buffer is discontinuous */
  if (discont) {
    GST_DEBUG_OBJECT (mpegvideoparse, "Have discont packet, draining data");
    mpegvideoparse->need_discont = TRUE;

    mpeg_packetiser_handle_eos (&mpegvideoparse->packer);
    res = mpegvideoparse_drain_avail (mpegvideoparse);
    mpeg_packetiser_flush (&mpegvideoparse->packer);
    if (res != GST_FLOW_OK) {
      gst_buffer_unref (buf);
      goto done;
    }
  }

  /* Takes ownership of the data */
  mpeg_packetiser_add_buf (&mpegvideoparse->packer, buf);

  /* And push out what we can */
  res = mpegvideoparse_drain_avail (mpegvideoparse);

done:
  /* Update our offset */
  mpegvideoparse->next_offset = next_offset;

  return res;
}

/* scan the decode queue for a picture header with an I frame and return the
 * index in the first buffer. We only scan the first buffer and possibly a
 * couple of bytes of the next buffers to find the I frame. Scanning is done
 * backwards because the first buffer could contain many picture start codes
 * with I frames. */
static guint
scan_keyframe (MpegVideoParse * mpegvideoparse)
{
  guint64 scanword;
  guint count;
  GList *walk;
  GstBuffer *head;
  guint8 *data;
  guint size;

  /* we use an 8 byte buffer, this is enough to hold the picture start code and
   * the picture header bits we are interested in. We init to 0xff so that when
   * we have a valid picture start without the header bits, we will be able to
   * detect this because it will generate an invalid picture type. */
  scanword = ~G_GUINT64_CONSTANT (0);

  GST_LOG_OBJECT (mpegvideoparse, "scan keyframe");

  /* move to the second buffer if we can, we should have at least one buffer */
  walk = mpegvideoparse->decode;
  g_return_val_if_fail (walk != NULL, -1);

  head = GST_BUFFER_CAST (walk->data);

  count = 0;
  walk = g_list_next (walk);
  while (walk) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    GST_LOG_OBJECT (mpegvideoparse, "collect remaining %d bytes from %p",
        6 - count, buf);

    while (size > 0 && count < 6) {
      scanword = (scanword << 8) | *data++;
      size--;
      count++;
    }
    if (count == 6)
      break;

    walk = g_list_next (walk);
  }
  /* move bits to the beginning of the word now */
  if (count)
    scanword = (scanword << (8 * (8 - count)));

  GST_LOG_OBJECT (mpegvideoparse, "scanword 0x%016" G_GINT64_MODIFIER "x",
      scanword);

  data = GST_BUFFER_DATA (head);
  size = GST_BUFFER_SIZE (head);

  while (size > 0) {
    scanword = (((guint64) data[size - 1]) << 56) | (scanword >> 8);

    GST_LOG_OBJECT (mpegvideoparse,
        "scanword at %d 0x%016" G_GINT64_MODIFIER "x", size - 1, scanword);

    /* check picture start and picture type */
    if ((scanword & G_GUINT64_CONSTANT (0xffffffff00380000)) ==
        G_GUINT64_CONSTANT (0x0000010000080000))
      break;

    size--;
  }
  return size - 1;
}

/* For reverse playback we use a technique that can be used for
 * any keyframe based video codec.
 *
 * Input:
 *  Buffer decoding order:  7  8  9  4  5  6  1  2  3  EOS
 *  Keyframe flag:                      K        K
 *  Discont flag:           D        D        D
 *
 * - Each Discont marks a discont in the decoding order.
 * - The keyframes mark where we can start decoding. For mpeg they are either
 *   set by the demuxer or we have to scan the buffers for a syncword and
 *   picture header with an I frame.
 *
 * First we prepend incomming buffers to the gather queue, whenever we receive
 * a discont, we flush out the gather queue.
 *
 * The above data will be accumulated in the gather queue like this:
 *
 *   gather queue:  9  8  7
 *                        D
 *
 * Whe buffer 4 is received (with a DISCONT), we flush the gather queue like
 * this:
 *
 *   while (gather)
 *     take head of queue and prepend to decode queue.
 *     if we copied a keyframe, decode the decode queue.
 *
 * After we flushed the gather queue, we add 4 to the (now empty) gather queue.
 * We get the following situation:
 *
 *  gather queue:    4
 *  decode queue:    7  8  9
 *
 * After we received 5 (Keyframe) and 6:
 *
 *  gather queue:    6  5  4
 *  decode queue:    7  8  9
 *
 * When we receive 1 (DISCONT) which triggers a flush of the gather queue:
 *
 *   Copy head of the gather queue (6) to decode queue:
 *
 *    gather queue:    5  4
 *    decode queue:    6  7  8  9
 *
 *   Copy head of the gather queue (5) to decode queue. This is a keyframe so we
 *   can start decoding.
 *
 *    gather queue:    4
 *    decode queue:    5  6  7  8  9
 *
 *   Decode frames in decode queue, we don't actually do the decoding but we
 *   will send the decode queue to the downstream element. This will empty the
 *   decoding queue again.
 *
 *   Copy head of the gather queue (4) to decode queue, we flushed the gather
 *   queue and can now store input buffer in the gather queue:
 *
 *    gather queue:    1
 *    decode queue:    4
 *
 *  When we receive EOS, the queue looks like:
 *
 *    gather queue:    3  2  1
 *    decode queue:    4
 *
 *  Fill decode queue, first keyframe we copy is 2:
 *
 *    gather queue:    1
 *    decode queue:    2  3  4
 *
 *  After pushing the decode queue:
 *
 *    gather queue:    1
 *    decode queue:    
 *
 *  Leftover buffer 1 cannot be decoded and must be discarded.
 */
static GstFlowReturn
gst_mpegvideoparse_flush_decode (MpegVideoParse * mpegvideoparse, guint idx)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer *head = NULL;

  while (mpegvideoparse->decode) {
    GstBuffer *buf;

    buf = GST_BUFFER_CAST (mpegvideoparse->decode->data);

    if (idx != -1) {
      GstBuffer *temp;

      if (idx > 0) {
        /* first buffer, split at the point where the picture start was
         * detected and store as the new head of the decoding list. */
        head = gst_buffer_create_sub (buf, 0, idx);
        /* push the remainder after the picture sync point downstream, this is the
         * first DISCONT buffer we push. */
        temp = gst_buffer_create_sub (buf, idx, GST_BUFFER_SIZE (buf) - idx);
        /* don't need old buffer anymore and swap new buffer */
        gst_buffer_unref (buf);
        buf = temp;
      }
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      idx = -1;
    } else {
      /* next buffers are not discont */
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
    }

    GST_DEBUG_OBJECT (mpegvideoparse, "pushing buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    res = gst_pad_push (mpegvideoparse->srcpad, buf);

    mpegvideoparse->decode =
        g_list_delete_link (mpegvideoparse->decode, mpegvideoparse->decode);
  }
  if (head) {
    /* store remainder of the buffer */
    mpegvideoparse->decode = g_list_prepend (mpegvideoparse->decode, head);
  }
  return res;
}

static GstFlowReturn
gst_mpegvideoparse_chain_reverse (MpegVideoParse * mpegvideoparse,
    gboolean discont, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (mpegvideoparse, "received discont,gathering buffers");

    while (mpegvideoparse->gather) {
      GstBuffer *gbuf;
      guint keyframeidx;

      gbuf = GST_BUFFER_CAST (mpegvideoparse->gather->data);
      /* remove from the gather list */
      mpegvideoparse->gather =
          g_list_delete_link (mpegvideoparse->gather, mpegvideoparse->gather);
      /* copy to decode queue */
      mpegvideoparse->decode = g_list_prepend (mpegvideoparse->decode, gbuf);

      GST_DEBUG_OBJECT (mpegvideoparse, "copied decoding buffer %p, len %d",
          gbuf, g_list_length (mpegvideoparse->decode));

      /* check if we copied a keyframe, we scan the buffers on the decode queue.
       * We only need to scan the first buffer and at most 3 bytes of the second
       * buffer. We return the index of the keyframe (or -1 when nothing was
       * found) */
      while ((keyframeidx = scan_keyframe (mpegvideoparse)) != -1) {
        GST_DEBUG_OBJECT (mpegvideoparse, "copied keyframe at %u", keyframeidx);
        res = gst_mpegvideoparse_flush_decode (mpegvideoparse, keyframeidx);
      }
    }
  }

  if (buf) {
    /* add buffer to gather queue */
    GST_DEBUG_OBJECT (mpegvideoparse, "gathering buffer %p, size %u", buf,
        GST_BUFFER_SIZE (buf));
    mpegvideoparse->gather = g_list_prepend (mpegvideoparse->gather, buf);
  }

  return res;
}

static GstFlowReturn
gst_mpegvideoparse_chain (GstPad * pad, GstBuffer * buf)
{
  MpegVideoParse *mpegvideoparse;
  GstFlowReturn res;
  gboolean discont;

  mpegvideoparse =
      GST_MPEGVIDEOPARSE (gst_object_get_parent (GST_OBJECT (pad)));

  discont = GST_BUFFER_IS_DISCONT (buf);

  if (mpegvideoparse->segment.rate > 0.0)
    res = gst_mpegvideoparse_chain_forward (mpegvideoparse, discont, buf);
  else
    res = gst_mpegvideoparse_chain_reverse (mpegvideoparse, discont, buf);

  gst_object_unref (mpegvideoparse);

  return res;
}

static gboolean
mpv_parse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  MpegVideoParse *mpegvideoparse =
      GST_MPEGVIDEOPARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, pos;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);

      if (format == GST_FORMAT_BYTES) {
        /* FIXME: Later, we might use a seek table to seek on elementary stream
           files, and that would allow byte-to-time conversions. It's not a high
           priority - most mpeg video is muxed and then the demuxer handles 
           seeking. In the meantime, here's some commented out logic copied
           from mp3parse */
#if 0
        GstClockTime seg_start, seg_stop, seg_pos;

        /* stop time is allowed to be open-ended, but not start & pos */
        if (!mp3parse_bytepos_to_time (mp3parse, stop, &seg_stop))
          seg_stop = GST_CLOCK_TIME_NONE;
        if (mp3parse_bytepos_to_time (mp3parse, start, &seg_start) &&
            mp3parse_bytepos_to_time (mp3parse, pos, &seg_pos)) {
          gst_event_unref (event);
          event = gst_event_new_new_segment_full (update, rate, applied_rate,
              GST_FORMAT_TIME, seg_start, seg_stop, seg_pos);
          format = GST_FORMAT_TIME;
          GST_DEBUG_OBJECT (mp3parse, "Converted incoming segment to TIME. "
              "start = %" G_GINT64_FORMAT ", stop = %" G_GINT64_FORMAT
              "pos = %" G_GINT64_FORMAT, seg_start, seg_stop, seg_pos);
        }
#endif
      }

      if (format != GST_FORMAT_TIME) {
        /* Unknown incoming segment format. Output a default open-ended 
         * TIME segment */
        gst_event_unref (event);

        /* set new values */
        format = GST_FORMAT_TIME;
        start = 0;
        stop = GST_CLOCK_TIME_NONE;
        pos = 0;
        /* create a new segment with these values */
        event = gst_event_new_new_segment_full (update, rate, applied_rate,
            format, start, stop, pos);
      }

      gst_mpegvideoparse_flush (mpegvideoparse);

      /* now configure the values */
      gst_segment_set_newsegment_full (&mpegvideoparse->segment, update,
          rate, applied_rate, format, start, stop, pos);

      GST_DEBUG_OBJECT (mpegvideoparse,
          "Pushing newseg rate %g, applied rate %g, format %d, start %"
          G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT ", pos %" G_GINT64_FORMAT,
          rate, applied_rate, format, start, stop, pos);

      /* Forward the event if we've seen a sequence header
       * and therefore set output caps, otherwise queue it for later */
      if (mpegvideoparse->seq_hdr.mpeg_version != 0)
        res = gst_pad_push_event (mpegvideoparse->srcpad, event);
      else {
        res = TRUE;
        mpegvideoparse->pending_segs =
            g_list_append (mpegvideoparse->pending_segs, event);
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (mpegvideoparse, "flush stop");
      gst_mpegvideoparse_flush (mpegvideoparse);
      res = gst_pad_push_event (mpegvideoparse->srcpad, event);
      break;
    case GST_EVENT_EOS:
      /* Push any remaining buffers out, then flush. */
      GST_DEBUG_OBJECT (mpegvideoparse, "received EOS");
      if (mpegvideoparse->segment.rate >= 0.0) {
        mpeg_packetiser_handle_eos (&mpegvideoparse->packer);
        mpegvideoparse_drain_avail (mpegvideoparse);
        gst_mpegvideoparse_flush (mpegvideoparse);
      } else {
        gst_mpegvideoparse_chain_reverse (mpegvideoparse, TRUE, NULL);
        gst_mpegvideoparse_flush_decode (mpegvideoparse, 0);
      }
      res = gst_pad_push_event (mpegvideoparse->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (mpegvideoparse->srcpad, event);
      break;
  }

  gst_object_unref (mpegvideoparse);
  return res;
}

static GstStateChangeReturn
gst_mpegvideoparse_change_state (GstElement * element,
    GstStateChange transition)
{
  MpegVideoParse *mpegvideoparse;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_MPEGVIDEOPARSE (element),
      GST_STATE_CHANGE_FAILURE);

  mpegvideoparse = GST_MPEGVIDEOPARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&mpegvideoparse->segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      mpv_parse_reset (mpegvideoparse);
      gst_mpegvideoparse_flush (mpegvideoparse);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpv_parse_debug, "legacympegvideoparse", 0,
      "MPEG Video Parser");

  return gst_element_register (plugin, "legacympegvideoparse",
      GST_RANK_NONE, GST_TYPE_MPEGVIDEOPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegvideoparse",
    "MPEG-1 and MPEG-2 video parser",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
