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
 * + Collect a list of regions and the sequence headers that apply
 *   to each region so that we properly handle SEQUENCE_END followed
 *   by a new sequence.
 * + At least detect when the sequence changes and error out instead.
 * + Do all the other stuff (documentation, tests) to get it into
 *   ugly or good.
 * + low priority:
 *   - handle seeking in raw elementary streams
 *   - calculate timestamps for all un-timestamped frames, taking into
 *     account frame re-ordering. Doing this probably requires introducing
 *     an extra end-to-end delay, however so might not be really desirable.
 */
GST_DEBUG_CATEGORY_STATIC (mpv_parse_debug);
#define GST_CAT_DEFAULT mpv_parse_debug

/* elementfactory information */
static GstElementDetails mpegvideoparse_details =
GST_ELEMENT_DETAILS ("MPEG video elementary stream parser",
    "Codec/Parser/Video",
    "Parses and frames MPEG-1 and MPEG-2 elementary video streams",
    "Wim Taymans <wim.taymans@chello.be>\n"
    "Jan Schmidt <thaytan@mad.scientist.com>");

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
static void gst_mpegvideoparse_dispose (MpegVideoParse * mpegvideoparse);

static GstFlowReturn gst_mpegvideoparse_chain (GstPad * pad, GstBuffer * buf);
static gboolean mpv_parse_sink_event (GstPad * pad, GstEvent * event);
static void gst_mpegvideoparse_flush (MpegVideoParse * mpegvideoparse);
static GstStateChangeReturn
gst_mpegvideoparse_change_state (GstElement * element,
    GstStateChange transition);

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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &mpegvideoparse_details);
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
gst_mpegvideoparse_dispose (MpegVideoParse * mpegvideoparse)
{
  mpeg_packetiser_free (&mpegvideoparse->packer);
  gst_buffer_replace (&mpegvideoparse->seq_hdr_buf, NULL);
}

/* Set the Pixel Aspect Ratio in our hdr from a DAR code in the data */
static void
set_par_from_dar (MPEGSeqHdr * hdr, guint8 asr_code)
{
  /* Pixel_width = DAR_width * display_vertical_size */
  /* Pixel_height = DAR_height * display_horizontal_size */
  switch (asr_code) {
    case 0x02:                 /* 3:4 DAR = 4:3 pixels */
      hdr->par_w = 4 * hdr->height;
      hdr->par_h = 3 * hdr->width;
      break;
    case 0x03:                 /* 9:16 DAR */
      hdr->par_w = 16 * hdr->height;
      hdr->par_h = 9 * hdr->width;
      break;
    case 0x04:                 /* 1:2.21 DAR */
      hdr->par_w = 221 * hdr->height;
      hdr->par_h = 100 * hdr->width;
      break;
    case 0x01:                 /* Square pixels */
    default:
      hdr->par_w = hdr->par_h = 1;
      break;
  }
}

static void
set_fps_from_code (MPEGSeqHdr * hdr, guint8 fps_code)
{
  const gint framerates[][2] = {
    {30, 1}, {24000, 1001}, {24, 1}, {25, 1},
    {30000, 1001}, {30, 1}, {50, 1}, {60000, 1001},
    {60, 1}, {30, 1}
  };

  if (fps_code < 10) {
    hdr->fps_n = framerates[fps_code][0];
    hdr->fps_d = framerates[fps_code][1];
  } else {
    /* Force a valid framerate */
    hdr->fps_n = 30;
    hdr->fps_d = 1;
  }
}

static void
mpegvideoparse_parse_seq (MpegVideoParse * mpegvideoparse, GstBuffer * buf)
{
  MPEGSeqHdr new_hdr;
  guint32 code;
  guint8 dar_idx, fps_idx;
  gint seq_data_length;
  guint32 sync_word = 0xffffffff;
  guint8 *cur, *end;
  gboolean constrained_flag;
  gboolean load_intra_flag;
  gboolean load_non_intra_flag;

  cur = GST_BUFFER_DATA (buf);
  end = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);

  if (GST_BUFFER_SIZE (buf) < 12)
    return;                     /* Too small to be a sequence header */

  seq_data_length = 12;         /* minimum length. */

  /* Skip the sync word */
  cur += 4;

  /* Parse the MPEG 1 bits */
  new_hdr.mpeg_version = 1;

  code = GST_READ_UINT32_BE (cur);
  new_hdr.width = (code >> 20) & 0xfff;
  new_hdr.height = (code >> 8) & 0xfff;

  dar_idx = (code >> 4) & 0xf;
  set_par_from_dar (&new_hdr, dar_idx);
  fps_idx = code & 0xf;
  set_fps_from_code (&new_hdr, fps_idx);

  constrained_flag = (cur[7] >> 2) & 0x01;
  load_intra_flag = (cur[7] >> 1) & 0x01;
  if (load_intra_flag) {
    seq_data_length += 64;      /* 8 rows of 8 bytes of intra matrix */
    if (GST_BUFFER_SIZE (buf) < seq_data_length)
      return;
    cur += 64;
  }

  load_non_intra_flag = cur[7] & 0x01;
  if (load_non_intra_flag) {
    seq_data_length += 64;      /* 8 rows of 8 bytes of non-intra matrix */
    if (GST_BUFFER_SIZE (buf) < seq_data_length)
      return;
    cur += 64;
  }

  /* Skip the rest of the MPEG-1 header */
  cur += 8;

  /* Read MPEG-2 sequence extensions */
  cur = mpeg_find_start_code (&sync_word, cur, end);
  while (cur != NULL) {
    /* Cur points at the last byte of the start code */
    if (cur[0] == MPEG_PACKET_EXTENSION) {
      guint8 ext_code;

      if ((end - cur - 1) < 1)
        return;                 /* short extension packet extension */

      ext_code = cur[1] >> 4;
      if (ext_code == MPEG_PACKET_EXT_SEQUENCE) {
        /* Parse a Sequence Extension */
        guint8 horiz_size_ext, vert_size_ext;
        guint8 fps_n_ext, fps_d_ext;

        if ((end - cur - 1) < 7)
          /* need at least 10 bytes, minus 3 for the start code 000001 */
          return;

        horiz_size_ext = ((cur[2] << 1) & 0x02) | ((cur[3] >> 7) & 0x01);
        vert_size_ext = (cur[3] >> 5) & 0x03;
        fps_n_ext = (cur[6] >> 5) & 0x03;
        fps_d_ext = cur[6] & 0x1f;

        new_hdr.fps_n *= (fps_n_ext + 1);
        new_hdr.fps_d *= (fps_d_ext + 1);
        new_hdr.width += (horiz_size_ext << 12);
        new_hdr.height += (vert_size_ext << 12);
      }

      new_hdr.mpeg_version = 2;
    }
    cur = mpeg_find_start_code (&sync_word, cur, end);
  }

  if (new_hdr.par_w != mpegvideoparse->seq_hdr.par_w ||
      new_hdr.par_h != mpegvideoparse->seq_hdr.par_h ||
      new_hdr.fps_n != mpegvideoparse->seq_hdr.fps_n ||
      new_hdr.fps_d != mpegvideoparse->seq_hdr.fps_d ||
      new_hdr.width != mpegvideoparse->seq_hdr.width ||
      new_hdr.height != mpegvideoparse->seq_hdr.height ||
      new_hdr.mpeg_version != mpegvideoparse->seq_hdr.mpeg_version) {
    GstCaps *caps;
    GstBuffer *seq_buf;

    /* Store the entire sequence header + sequence header extension
       for output as codec_data */
    seq_buf = gst_buffer_copy (buf);
    gst_buffer_replace (&mpegvideoparse->seq_hdr_buf, seq_buf);
    gst_buffer_unref (seq_buf);

    /* And update the new_hdr into our stored version */
    memcpy (&mpegvideoparse->seq_hdr, &new_hdr, sizeof (MPEGSeqHdr));

    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "parsed", G_TYPE_BOOLEAN, TRUE,
        "mpegversion", G_TYPE_INT, new_hdr.mpeg_version,
        "width", G_TYPE_INT, new_hdr.width,
        "height", G_TYPE_INT, new_hdr.height,
        "framerate", GST_TYPE_FRACTION, new_hdr.fps_n, new_hdr.fps_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, new_hdr.par_w, new_hdr.par_h,
        "codec_data", GST_TYPE_BUFFER, seq_buf, NULL);

    GST_DEBUG ("New mpegvideoparse caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (mpegvideoparse->srcpad, caps);
  }
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
}

static GstFlowReturn
mpegvideoparse_drain_avail (MpegVideoParse * mpegvideoparse)
{
  MPEGBlockInfo *cur;
  GstBuffer *buf;
  GstFlowReturn res = GST_FLOW_OK;

  cur = mpeg_packetiser_get_block (&mpegvideoparse->packer, &buf);
  while (cur != NULL) {
    /* Handle the block */
    GST_LOG_OBJECT (mpegvideoparse,
        "Have block of size %u with pack_type 0x%02x and flags 0x%02x\n",
        cur->length, cur->first_pack_type, cur->flags);

    /* Don't start pushing out buffers until we've seen a sequence header */
    if (mpegvideoparse->seq_hdr.mpeg_version == 0) {
      if ((cur->flags & MPEG_BLOCK_FLAG_SEQUENCE) == 0) {
        if (buf) {
          GST_DEBUG_OBJECT (mpegvideoparse,
              "No sequence header yet. Dropping buffer of %u bytes",
              GST_BUFFER_SIZE (buf));
          gst_buffer_unref (buf);
          buf = NULL;
        }
      } else {
        /* Found a sequence header */
        mpegvideoparse_parse_seq (mpegvideoparse, buf);
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
      res = gst_pad_push (mpegvideoparse->srcpad, buf);
      if (res != GST_FLOW_OK)
        break;
    }

    /* Advance to the next data block */
    mpeg_packetiser_next_block (&mpegvideoparse->packer);
    cur = mpeg_packetiser_get_block (&mpegvideoparse->packer, &buf);
  };

  return res;
}

static GstFlowReturn
gst_mpegvideoparse_chain (GstPad * pad, GstBuffer * buf)
{
  MpegVideoParse *mpegvideoparse;
  GstFlowReturn res;
  gboolean have_discont;
  gint64 next_offset = GST_BUFFER_OFFSET_NONE;

  g_return_val_if_fail (pad != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  mpegvideoparse =
      GST_MPEGVIDEOPARSE (gst_object_get_parent (GST_OBJECT (pad)));

  GST_DEBUG_OBJECT (mpegvideoparse,
      "mpegvideoparse: received buffer of %u bytes with ts %"
      GST_TIME_FORMAT, GST_BUFFER_SIZE (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  /* If we have an offset, and the incoming offset doesn't match, 
     or we have a discont, handle it first by flushing out data
     we have collected. */
  have_discont = GST_BUFFER_IS_DISCONT (buf);

  if (mpegvideoparse->next_offset != GST_BUFFER_OFFSET_NONE) {
    if (GST_BUFFER_OFFSET_IS_VALID (buf)) {
      if (mpegvideoparse->next_offset != GST_BUFFER_OFFSET (buf))
        have_discont = TRUE;
      next_offset = GST_BUFFER_OFFSET (buf) + GST_BUFFER_SIZE (buf);
    } else {
      next_offset = mpegvideoparse->next_offset + GST_BUFFER_SIZE (buf);
    }
  }

  if (have_discont) {
    GST_DEBUG_OBJECT (mpegvideoparse, "Have discont packet, draining data");
    mpegvideoparse->need_discont = TRUE;

    mpeg_packetiser_handle_eos (&mpegvideoparse->packer);
    res = mpegvideoparse_drain_avail (mpegvideoparse);
    mpeg_packetiser_flush (&mpegvideoparse->packer);
    if (res != GST_FLOW_OK) {
      mpegvideoparse->next_offset = next_offset;
      gst_buffer_unref (buf);
      return res;
    }
  }

  /* Takes ownership of the data */
  mpeg_packetiser_add_buf (&mpegvideoparse->packer, buf);

  /* And push out what we can */
  res = mpegvideoparse_drain_avail (mpegvideoparse);

  /* Update our offset */
  mpegvideoparse->next_offset = next_offset;

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
        event = gst_event_new_new_segment_full (update, rate, applied_rate,
            GST_FORMAT_TIME, 0, GST_CLOCK_TIME_NONE, 0);
      }

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);
      GST_DEBUG_OBJECT (mpegvideoparse,
          "Pushing newseg rate %g, applied rate %g, "
          "format %d, start %lld, stop %lld, pos %lld\n",
          rate, applied_rate, format, start, stop, pos);

      res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_mpegvideoparse_flush (mpegvideoparse);
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_EOS:

      /* Push any remaining buffers out, then flush. */
      mpeg_packetiser_handle_eos (&mpegvideoparse->packer);
      mpegvideoparse_drain_avail (mpegvideoparse);
      gst_mpegvideoparse_flush (mpegvideoparse);

      res = gst_pad_event_default (pad, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
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

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      mpv_parse_reset (mpegvideoparse);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpv_parse_debug, "mpegvideoparse", 0,
      "MPEG Video Parser");

  return gst_element_register (plugin, "mpegvideoparse",
      GST_RANK_SECONDARY - 1, GST_TYPE_MPEGVIDEOPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegvideoparse",
    "MPEG-1 and MPEG-2 video parser",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
