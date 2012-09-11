/* GStreamer
 * Copyright (C) <2005> Jan Schmidt <jan@fluendo.com>
 * Copyright (C) <2002> Wim Taymans <wim@fluendo.com>
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

/* TODO: liboil-ise code, esp. use _splat() family of functions */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdvdsubdec.h"
#include "gstdvdsubparse.h"
#include <string.h>

#define gst_dvd_sub_dec_parent_class parent_class
G_DEFINE_TYPE (GstDvdSubDec, gst_dvd_sub_dec, GST_TYPE_ELEMENT);

static gboolean gst_dvd_sub_dec_src_event (GstPad * srcpad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_dvd_sub_dec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static gboolean gst_dvd_sub_dec_handle_dvd_event (GstDvdSubDec * dec,
    GstEvent * event);
static void gst_dvd_sub_dec_finalize (GObject * gobject);
static void gst_setup_palette (GstDvdSubDec * dec);
static void gst_dvd_sub_dec_merge_title (GstDvdSubDec * dec,
    GstVideoFrame * frame);
static GstClockTime gst_dvd_sub_dec_get_event_delay (GstDvdSubDec * dec);
static gboolean gst_dvd_sub_dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dvd_sub_dec_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_send_subtitle_frame (GstDvdSubDec * dec,
    GstClockTime end_ts);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format = (string) { AYUV, ARGB },"
        "width = (int) 720, height = (int) 576, framerate = (fraction) 0/1")
    );

static GstStaticPadTemplate subtitle_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subpicture/x-dvd")
    );

GST_DEBUG_CATEGORY_STATIC (gst_dvd_sub_dec_debug);
#define GST_CAT_DEFAULT (gst_dvd_sub_dec_debug)

enum
{
  SPU_FORCE_DISPLAY = 0x00,
  SPU_SHOW = 0x01,
  SPU_HIDE = 0x02,
  SPU_SET_PALETTE = 0x03,
  SPU_SET_ALPHA = 0x04,
  SPU_SET_SIZE = 0x05,
  SPU_SET_OFFSETS = 0x06,
  SPU_WIPE = 0x07,
  SPU_END = 0xff
};

static const guint32 default_clut[16] = {
  0xb48080, 0x248080, 0x628080, 0xd78080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080
};

typedef struct RLE_state
{
  gint id;
  gint aligned;
  gint offset[2];
  gint hl_left;
  gint hl_right;

  guchar *target;

  guchar next;
}
RLE_state;

static void
gst_dvd_sub_dec_class_init (GstDvdSubDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_dvd_sub_dec_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&subtitle_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "DVD subtitle decoder", "Codec/Decoder/Video",
      "Decodes DVD subtitles into AYUV video frames",
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Jan Schmidt <thaytan@mad.scientist.com>");
}

static void
gst_dvd_sub_dec_init (GstDvdSubDec * dec)
{
  GstPadTemplate *tmpl;

  dec->sinkpad = gst_pad_new_from_static_template (&subtitle_template, "sink");
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvd_sub_dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvd_sub_dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  tmpl = gst_static_pad_template_get (&src_template);
  dec->srcpad = gst_pad_new_from_template (tmpl, "src");
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_dvd_sub_dec_src_event));
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_object_unref (tmpl);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  /* FIXME: aren't there more possible sizes? (tpm) */
  dec->in_width = 720;
  dec->in_height = 576;

  dec->partialbuf = NULL;
  dec->have_title = FALSE;
  dec->parse_pos = NULL;
  dec->forced_display = FALSE;
  dec->visible = FALSE;

  memcpy (dec->current_clut, default_clut, sizeof (guint32) * 16);

  gst_setup_palette (dec);

  dec->next_ts = 0;
  dec->next_event_ts = GST_CLOCK_TIME_NONE;

  dec->buf_dirty = TRUE;
  dec->use_ARGB = FALSE;
}

static void
gst_dvd_sub_dec_finalize (GObject * gobject)
{
  GstDvdSubDec *dec = GST_DVD_SUB_DEC (gobject);

  if (dec->partialbuf) {
    gst_buffer_unmap (dec->partialbuf, &dec->partialmap);
    gst_buffer_unref (dec->partialbuf);
    dec->partialbuf = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static gboolean
gst_dvd_sub_dec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static GstClockTime
gst_dvd_sub_dec_get_event_delay (GstDvdSubDec * dec)
{
  guchar *buf;
  guint16 ticks;
  GstClockTime event_delay;

  /* If starting a new buffer, follow the first DCSQ ptr */
  if (dec->parse_pos == dec->partialmap.data) {
    buf = dec->parse_pos + dec->data_size;
  } else {
    buf = dec->parse_pos;
  }

  ticks = GST_READ_UINT16_BE (buf);
  event_delay = gst_util_uint64_scale (ticks, 1024 * GST_SECOND, 90000);

  GST_DEBUG_OBJECT (dec, "returning delay %" GST_TIME_FORMAT " from offset %u",
      GST_TIME_ARGS (event_delay), (guint) (buf - dec->parse_pos));

  return event_delay;
}

/*
 * Parse the next event time in the current subpicture buffer, stopping
 * when time advances to the next state. 
 */
static void
gst_dvd_sub_dec_parse_subpic (GstDvdSubDec * dec)
{
#define PARSE_BYTES_NEEDED(x) if ((buf+(x)) >= end) \
  { GST_WARNING("Subtitle stream broken parsing %c", *buf); \
    broken = TRUE; break; }

  guchar *start = dec->partialmap.data;
  guchar *buf;
  guchar *end;
  gboolean broken = FALSE;
  gboolean last_seq = FALSE;
  guchar *next_seq = NULL;
  GstClockTime event_time;

  /* nothing to do if we finished this buffer already */
  if (dec->parse_pos == NULL)
    return;

  g_return_if_fail (dec->packet_size >= 4);

  end = start + dec->packet_size;
  if (dec->parse_pos == start) {
    buf = dec->parse_pos + dec->data_size;
  } else {
    buf = dec->parse_pos;
  }

  g_assert (buf >= start && buf < end);

  /* If the next control sequence is at the current offset, this is 
   * the last one */
  next_seq = start + GST_READ_UINT16_BE (buf + 2);
  last_seq = (next_seq == buf);
  buf += 4;

  while ((buf < end) && (!broken)) {
    switch (*buf) {
      case SPU_FORCE_DISPLAY:  /* Forced display menu subtitle */
        dec->forced_display = TRUE;
        dec->buf_dirty = TRUE;
        GST_DEBUG_OBJECT (dec, "SPU FORCE_DISPLAY");
        buf++;
        break;
      case SPU_SHOW:           /* Show the subtitle in this packet */
        dec->visible = TRUE;
        dec->buf_dirty = TRUE;
        GST_DEBUG_OBJECT (dec, "SPU SHOW at %" GST_TIME_FORMAT,
            GST_TIME_ARGS (dec->next_event_ts));
        buf++;
        break;
      case SPU_HIDE:
        /* 02 ff (ff) is the end of the packet, hide the subpicture */
        dec->visible = FALSE;
        dec->buf_dirty = TRUE;

        GST_DEBUG_OBJECT (dec, "SPU HIDE at %" GST_TIME_FORMAT,
            GST_TIME_ARGS (dec->next_event_ts));
        buf++;
        break;
      case SPU_SET_PALETTE:    /* palette */
        PARSE_BYTES_NEEDED (3);

        GST_DEBUG_OBJECT (dec, "SPU SET_PALETTE");

        dec->subtitle_index[3] = buf[1] >> 4;
        dec->subtitle_index[2] = buf[1] & 0xf;
        dec->subtitle_index[1] = buf[2] >> 4;
        dec->subtitle_index[0] = buf[2] & 0xf;
        gst_setup_palette (dec);

        dec->buf_dirty = TRUE;
        buf += 3;
        break;
      case SPU_SET_ALPHA:      /* transparency palette */
        PARSE_BYTES_NEEDED (3);

        GST_DEBUG_OBJECT (dec, "SPU SET_ALPHA");

        dec->subtitle_alpha[3] = buf[1] >> 4;
        dec->subtitle_alpha[2] = buf[1] & 0xf;
        dec->subtitle_alpha[1] = buf[2] >> 4;
        dec->subtitle_alpha[0] = buf[2] & 0xf;
        gst_setup_palette (dec);

        dec->buf_dirty = TRUE;
        buf += 3;
        break;
      case SPU_SET_SIZE:       /* image coordinates */
        PARSE_BYTES_NEEDED (7);

        dec->top = ((buf[4] & 0x3f) << 4) | ((buf[5] & 0xe0) >> 4);
        dec->left = ((buf[1] & 0x3f) << 4) | ((buf[2] & 0xf0) >> 4);
        dec->right = ((buf[2] & 0x03) << 8) | buf[3];
        dec->bottom = ((buf[5] & 0x03) << 8) | buf[6];

        GST_DEBUG_OBJECT (dec, "SPU SET_SIZE left %d, top %d, right %d, "
            "bottom %d", dec->left, dec->top, dec->right, dec->bottom);

        dec->buf_dirty = TRUE;
        buf += 7;
        break;
      case SPU_SET_OFFSETS:    /* image 1 / image 2 offsets */
        PARSE_BYTES_NEEDED (5);

        dec->offset[0] = (((guint) buf[1]) << 8) | buf[2];
        dec->offset[1] = (((guint) buf[3]) << 8) | buf[4];
        GST_DEBUG_OBJECT (dec, "Offset1 %d, Offset2 %d",
            dec->offset[0], dec->offset[1]);

        dec->buf_dirty = TRUE;
        buf += 5;
        break;
      case SPU_WIPE:
      {
        guint length;

        PARSE_BYTES_NEEDED (3);

        GST_WARNING_OBJECT (dec, "SPU_WIPE not yet implemented");

        length = (buf[1] << 8) | (buf[2]);
        buf += 1 + length;

        dec->buf_dirty = TRUE;
        break;
      }
      case SPU_END:
        buf = (last_seq) ? end : next_seq;

        /* Start a new control sequence */
        if (buf + 4 < end) {
          guint16 ticks = GST_READ_UINT16_BE (buf);

          event_time = gst_util_uint64_scale (ticks, 1024 * GST_SECOND, 90000);

          GST_DEBUG_OBJECT (dec,
              "Next DCSQ at offset %u, delay %g secs (%d ticks)",
              (guint) (buf - start),
              gst_util_guint64_to_gdouble (event_time / GST_SECOND), ticks);

          dec->parse_pos = buf;
          if (event_time > 0) {
            dec->next_event_ts += event_time;

            GST_LOG_OBJECT (dec, "Exiting parse loop with time %g",
                gst_guint64_to_gdouble (dec->next_event_ts) /
                gst_guint64_to_gdouble (GST_SECOND));
            return;
          }
        } else {
          dec->parse_pos = NULL;
          dec->next_event_ts = GST_CLOCK_TIME_NONE;
          GST_LOG_OBJECT (dec, "Finished all cmds. Exiting parse loop");
          return;
        }
      default:
        GST_ERROR
            ("Invalid sequence in subtitle packet header (%.2x). Skipping",
            *buf);
        broken = TRUE;
        dec->parse_pos = NULL;
        break;
    }
  }
}

static inline int
gst_get_nibble (guchar * buffer, RLE_state * state)
{
  if (state->aligned) {
    state->next = buffer[state->offset[state->id]++];
    state->aligned = 0;
    return state->next >> 4;
  } else {
    state->aligned = 1;
    return state->next & 0xf;
  }
}

/* Premultiply the current lookup table into the "target" cache */
static void
gst_setup_palette (GstDvdSubDec * dec)
{
  gint i;
  guint32 col;
  Color_val *target_yuv = dec->palette_cache_yuv;
  Color_val *target2_yuv = dec->hl_palette_cache_yuv;
  Color_val *target_rgb = dec->palette_cache_rgb;
  Color_val *target2_rgb = dec->hl_palette_cache_rgb;

  for (i = 0; i < 4; i++, target2_yuv++, target_yuv++) {
    col = dec->current_clut[dec->subtitle_index[i]];
    target_yuv->Y_R = (col >> 16) & 0xff;
    target_yuv->V_B = (col >> 8) & 0xff;
    target_yuv->U_G = col & 0xff;
    target_yuv->A = dec->subtitle_alpha[i] * 0xff / 0xf;

    col = dec->current_clut[dec->menu_index[i]];
    target2_yuv->Y_R = (col >> 16) & 0xff;
    target2_yuv->V_B = (col >> 8) & 0xff;
    target2_yuv->U_G = col & 0xff;
    target2_yuv->A = dec->menu_alpha[i] * 0xff / 0xf;

    /* If ARGB flag set, then convert YUV palette to RGB */
    /* Using integer aritmetic */
    if (dec->use_ARGB) {
      guchar C = target_yuv->Y_R - 16;
      guchar D = target_yuv->U_G - 128;
      guchar E = target_yuv->V_B - 128;

      target_rgb->Y_R = CLAMP (((298 * C + 409 * E + 128) >> 8), 0, 255);
      target_rgb->U_G =
          CLAMP (((298 * C - 100 * D - 128 * E + 128) >> 8), 0, 255);
      target_rgb->V_B = CLAMP (((298 * C + 516 * D + 128) >> 8), 0, 255);
      target_rgb->A = target_yuv->A;

      C = target2_yuv->Y_R - 16;
      D = target2_yuv->U_G - 128;
      E = target2_yuv->V_B - 128;

      target2_rgb->Y_R = CLAMP (((298 * C + 409 * E + 128) >> 8), 0, 255);
      target2_rgb->U_G =
          CLAMP (((298 * C - 100 * D - 128 * E + 128) >> 8), 0, 255);
      target2_rgb->V_B = CLAMP (((298 * C + 516 * D + 128) >> 8), 0, 255);
      target2_rgb->A = target2_yuv->A;
    }
    target_rgb++;
    target2_rgb++;
  }
}

static inline guint
gst_get_rle_code (guchar * buffer, RLE_state * state)
{
  gint code;

  code = gst_get_nibble (buffer, state);
  if (code < 0x4) {             /* 4 .. f */
    code = (code << 4) | gst_get_nibble (buffer, state);
    if (code < 0x10) {          /* 1x .. 3x */
      code = (code << 4) | gst_get_nibble (buffer, state);
      if (code < 0x40) {        /* 04x .. 0fx */
        code = (code << 4) | gst_get_nibble (buffer, state);
      }
    }
  }
  return code;
}

#define DRAW_RUN(target,len,c)                  \
G_STMT_START {                                  \
  gint i = 0;                                   \
  if ((c)->A) {                                 \
    for (i = 0; i < (len); i++) {               \
      *(target)++ = (c)->A;                     \
      *(target)++ = (c)->Y_R;                   \
      *(target)++ = (c)->U_G;                   \
      *(target)++ = (c)->V_B;                   \
    }                                           \
  } else {                                      \
    (target) += 4 * (len);                      \
  }                                             \
} G_STMT_END

/* 
 * This function steps over each run-length segment, drawing 
 * into the YUVA/ARGB buffers as it goes. UV are composited and then output
 * at half width/height
 */
static void
gst_draw_rle_line (GstDvdSubDec * dec, guchar * buffer, RLE_state * state)
{
  gint length, colourid;
  guint code;
  gint x, right;
  guchar *target;

  target = state->target;

  x = dec->left;
  right = dec->right + 1;

  while (x < right) {
    gboolean in_hl;
    const Color_val *colour_entry;

    code = gst_get_rle_code (buffer, state);
    length = code >> 2;
    colourid = code & 3;
    if (dec->use_ARGB)
      colour_entry = dec->palette_cache_rgb + colourid;
    else
      colour_entry = dec->palette_cache_yuv + colourid;

    /* Length = 0 implies fill to the end of the line */
    /* Restrict the colour run to the end of the line */
    if (length == 0 || x + length > right)
      length = right - x;

    /* Check if this run of colour touches the highlight region */
    in_hl = ((x <= state->hl_right) && (x + length) >= state->hl_left);
    if (in_hl) {
      gint run;

      /* Draw to the left of the highlight */
      if (x <= state->hl_left) {
        run = MIN (length, state->hl_left - x + 1);

        DRAW_RUN (target, run, colour_entry);
        length -= run;
        x += run;
      }

      /* Draw across the highlight region */
      if (x <= state->hl_right) {
        const Color_val *hl_colour;
        if (dec->use_ARGB)
          hl_colour = dec->hl_palette_cache_rgb + colourid;
        else
          hl_colour = dec->hl_palette_cache_yuv + colourid;

        run = MIN (length, state->hl_right - x + 1);

        DRAW_RUN (target, run, hl_colour);
        length -= run;
        x += run;
      }
    }

    /* Draw the rest of the run */
    if (length > 0) {
      DRAW_RUN (target, length, colour_entry);
      x += length;
    }
  }
}

/*
 * Decode the RLE subtitle image and blend with the current
 * frame buffer.
 */
static void
gst_dvd_sub_dec_merge_title (GstDvdSubDec * dec, GstVideoFrame * frame)
{
  gint y;
  gint Y_stride;
  guchar *buffer = dec->partialmap.data;
  gint hl_top, hl_bottom;
  gint last_y;
  RLE_state state;
  guint8 *Y_data;;

  GST_DEBUG_OBJECT (dec, "Merging subtitle on frame");

  Y_data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  Y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  state.id = 0;
  state.aligned = 1;
  state.next = 0;
  state.offset[0] = dec->offset[0];
  state.offset[1] = dec->offset[1];

  /* center the image when display rectangle exceeds the video width */
  if (dec->in_width <= dec->right) {
    gint left, disp_width;

    disp_width = dec->right - dec->left + 1;
    left = (dec->in_width - disp_width) / 2;
    dec->left = left;
    dec->right = left + disp_width - 1;

    /* if it clips to the right, shift it left, but only till zero */
    if (dec->right >= dec->in_width) {
      gint shift = dec->right - dec->in_width - 1;
      if (shift > dec->left)
        shift = dec->left;
      dec->left -= shift;
      dec->right -= shift;
    }

    GST_DEBUG_OBJECT (dec, "clipping width to %d,%d",
        dec->left, dec->in_width - 1);
  }

  /* for the height, bring it up till it fits as well as it can. We
   * assume the picture is in the lower part. We should better check where it
   * is and do something more clever. */
  if (dec->in_height <= dec->bottom) {

    /* shift it up, but only till zero */
    gint shift = dec->bottom - dec->in_height - 1;
    if (shift > dec->top)
      shift = dec->top;
    dec->top -= shift;
    dec->bottom -= shift;

    /* start on even line */
    if (dec->top & 1) {
      dec->top--;
      dec->bottom--;
    }

    GST_DEBUG_OBJECT (dec, "clipping height to %d,%d",
        dec->top, dec->in_height - 1);
  }

  if (dec->current_button) {
    hl_top = dec->hl_top;
    hl_bottom = dec->hl_bottom;
  } else {
    hl_top = -1;
    hl_bottom = -1;
  }
  last_y = MIN (dec->bottom, dec->in_height);

  y = dec->top;
  state.target = Y_data + 4 * dec->left + (y * Y_stride);

  /* Now draw scanlines until we hit last_y or end of RLE data */
  for (; ((state.offset[1] < dec->data_size + 2) && (y <= last_y)); y++) {
    /* Set up to draw the highlight if we're in the right scanlines */
    if (y > hl_bottom || y < hl_top) {
      state.hl_left = -1;
      state.hl_right = -1;
    } else {
      state.hl_left = dec->hl_left;
      state.hl_right = dec->hl_right;
    }
    gst_draw_rle_line (dec, buffer, &state);

    state.target += Y_stride;

    /* Realign the RLE state for the next line */
    if (!state.aligned)
      gst_get_nibble (buffer, &state);
    state.id = !state.id;
  }
}

static void
gst_send_empty_fill (GstDvdSubDec * dec, GstClockTime ts)
{
  if (dec->next_ts < ts) {
    GST_LOG_OBJECT (dec, "Sending GAP event update to advance time to %"
        GST_TIME_FORMAT, GST_TIME_ARGS (ts));

    gst_pad_push_event (dec->srcpad,
        gst_event_new_gap (dec->next_ts, ts - dec->next_ts));
  }
  dec->next_ts = ts;
}

static GstFlowReturn
gst_send_subtitle_frame (GstDvdSubDec * dec, GstClockTime end_ts)
{
  GstFlowReturn flow;
  GstBuffer *out_buf;
  GstVideoFrame frame;
  guint8 *data;
  gint x, y;
  static GstAllocationParams params = { 0, 3, 0, 0, };

  g_assert (dec->have_title);
  g_assert (dec->next_ts <= end_ts);

  /* Check if we need to redraw the output buffer */
  if (!dec->buf_dirty) {
    flow = GST_FLOW_OK;
    goto out;
  }

  out_buf =
      gst_buffer_new_allocate (NULL, 4 * GST_VIDEO_INFO_SIZE (&dec->info),
      &params);
  gst_video_frame_map (&frame, &dec->info, out_buf, GST_MAP_READWRITE);

  data = GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);

  /* Clear the buffer */
  /* FIXME - move this into the buffer rendering code */
  for (y = 0; y < dec->in_height; y++) {
    guchar *line = data + 4 * dec->in_width * y;

    for (x = 0; x < dec->in_width; x++) {
      line[0] = 0;              /* A */
      if (!dec->use_ARGB) {
        line[1] = 16;           /* Y */
        line[2] = 128;          /* U */
        line[3] = 128;          /* V */
      } else {
        line[1] = 0;            /* R */
        line[2] = 0;            /* G */
        line[3] = 0;            /* B */
      }

      line += 4;
    }
  }

  /* FIXME: do we really want to honour the forced_display flag
   * for subtitles streans? */
  if (dec->visible || dec->forced_display) {
    gst_dvd_sub_dec_merge_title (dec, &frame);
  }

  gst_video_frame_unmap (&frame);

  dec->buf_dirty = FALSE;

  GST_BUFFER_TIMESTAMP (out_buf) = dec->next_ts;
  if (GST_CLOCK_TIME_IS_VALID (dec->next_event_ts)) {
    GST_BUFFER_DURATION (out_buf) = GST_CLOCK_DIFF (dec->next_ts,
        dec->next_event_ts);
  } else {
    GST_BUFFER_DURATION (out_buf) = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG_OBJECT (dec, "Sending subtitle buffer with ts %"
      GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out_buf)),
      GST_BUFFER_DURATION (out_buf));

  flow = gst_pad_push (dec->srcpad, out_buf);

out:
  dec->next_ts = end_ts;
  return flow;
}

/* Walk time forward, processing any subtitle events as needed. */
static GstFlowReturn
gst_dvd_sub_dec_advance_time (GstDvdSubDec * dec, GstClockTime new_ts)
{
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (dec, "Advancing time to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (new_ts));

  if (!dec->have_title) {
    gst_send_empty_fill (dec, new_ts);
    return ret;
  }

  while (dec->next_ts < new_ts) {
    GstClockTime next_ts = new_ts;

    if (GST_CLOCK_TIME_IS_VALID (dec->next_event_ts) &&
        dec->next_event_ts < next_ts) {
      /* We might need to process the subtitle cmd queue */
      next_ts = dec->next_event_ts;
    }

    /* 
     * Now, either output a filler or a frame spanning
     * dec->next_ts to next_ts
     */
    if (dec->visible || dec->forced_display) {
      ret = gst_send_subtitle_frame (dec, next_ts);
    } else {
      gst_send_empty_fill (dec, next_ts);
    }

    /*
     * and then process some subtitle cmds if we need
     */
    if (next_ts == dec->next_event_ts)
      gst_dvd_sub_dec_parse_subpic (dec);
  }

  return ret;
}

static GstFlowReturn
gst_dvd_sub_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstDvdSubDec *dec;
  guint8 *data;
  glong size = 0;

  dec = GST_DVD_SUB_DEC (parent);

  GST_DEBUG_OBJECT (dec, "Have buffer of size %" G_GSIZE_FORMAT ", ts %"
      GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, gst_buffer_get_size (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_DURATION (buf));

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    if (!GST_CLOCK_TIME_IS_VALID (dec->next_ts)) {
      dec->next_ts = GST_BUFFER_TIMESTAMP (buf);
    }

    /* Move time forward to the start of the new buffer */
    ret = gst_dvd_sub_dec_advance_time (dec, GST_BUFFER_TIMESTAMP (buf));
  }

  if (dec->have_title) {
    gst_buffer_unmap (dec->partialbuf, &dec->partialmap);
    gst_buffer_unref (dec->partialbuf);
    dec->partialbuf = NULL;
    dec->have_title = FALSE;
  }

  GST_DEBUG_OBJECT (dec, "Got subtitle buffer, pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  /* deal with partial frame from previous buffer */
  if (dec->partialbuf) {
    gst_buffer_unmap (dec->partialbuf, &dec->partialmap);
    dec->partialbuf = gst_buffer_append (dec->partialbuf, buf);
  } else {
    dec->partialbuf = buf;
  }

  gst_buffer_map (dec->partialbuf, &dec->partialmap, GST_MAP_READ);

  data = dec->partialmap.data;
  size = dec->partialmap.size;

  if (size > 4) {
    dec->packet_size = GST_READ_UINT16_BE (data);

    if (dec->packet_size == size) {
      GST_LOG_OBJECT (dec, "Subtitle packet size %d, current size %ld",
          dec->packet_size, size);

      dec->data_size = GST_READ_UINT16_BE (data + 2);

      /* Reset parameters for a new subtitle buffer */
      dec->parse_pos = data;
      dec->forced_display = FALSE;
      dec->visible = FALSE;

      dec->have_title = TRUE;
      dec->next_event_ts = GST_BUFFER_TIMESTAMP (dec->partialbuf);

      if (!GST_CLOCK_TIME_IS_VALID (dec->next_event_ts))
        dec->next_event_ts = dec->next_ts;

      dec->next_event_ts += gst_dvd_sub_dec_get_event_delay (dec);
    }
  }

  return ret;
}

static gboolean
gst_dvd_sub_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDvdSubDec *dec = GST_DVD_SUB_DEC (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  GstCaps *out_caps = NULL, *peer_caps = NULL;

  GST_DEBUG_OBJECT (dec, "setcaps called with %" GST_PTR_FORMAT, caps);

  out_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "AYUV",
      "width", G_TYPE_INT, dec->in_width,
      "height", G_TYPE_INT, dec->in_height,
      "framerate", GST_TYPE_FRACTION, 0, 1, NULL);

  peer_caps = gst_pad_get_allowed_caps (dec->srcpad);
  if (G_LIKELY (peer_caps)) {
    guint i = 0, n = 0;

    n = gst_caps_get_size (peer_caps);
    GST_DEBUG_OBJECT (dec, "peer allowed caps (%u structure(s)) are %"
        GST_PTR_FORMAT, n, peer_caps);

    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peer_caps, i);
      /* Check if the peer pad support ARGB format, if yes change caps */
      if (gst_structure_has_name (s, "video/x-raw")) {
        gst_caps_unref (out_caps);
        GST_DEBUG_OBJECT (dec, "trying with ARGB");

        out_caps = gst_caps_new_simple ("video/x-raw",
            "format", G_TYPE_STRING, "ARGB",
            "width", G_TYPE_INT, dec->in_width,
            "height", G_TYPE_INT, dec->in_height,
            "framerate", GST_TYPE_FRACTION, 0, 1, NULL);

        if (gst_pad_peer_query_accept_caps (dec->srcpad, out_caps)) {
          GST_DEBUG_OBJECT (dec, "peer accepted ARGB");
          /* If ARGB format then set the flag */
          dec->use_ARGB = TRUE;
          break;
        }
      }
    }
    gst_caps_unref (peer_caps);
  }
  GST_DEBUG_OBJECT (dec, "setting caps downstream to %" GST_PTR_FORMAT,
      out_caps);
  if (gst_pad_set_caps (dec->srcpad, out_caps)) {
    gst_video_info_from_caps (&dec->info, out_caps);
  } else {
    GST_WARNING_OBJECT (dec, "failed setting downstream caps");
    gst_caps_unref (out_caps);
    goto beach;
  }

  gst_caps_unref (out_caps);
  ret = TRUE;

beach:
  gst_object_unref (dec);
  return ret;
}

static gboolean
gst_dvd_sub_dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDvdSubDec *dec = GST_DVD_SUB_DEC (parent);
  gboolean ret = FALSE;

  GST_LOG_OBJECT (dec, "%s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_dvd_sub_dec_sink_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:{
      GstClockTime ts = GST_EVENT_TIMESTAMP (event);

      if (gst_event_has_name (event, "application/x-gst-dvd")) {
        if (GST_CLOCK_TIME_IS_VALID (ts))
          gst_dvd_sub_dec_advance_time (dec, ts);

        if (gst_dvd_sub_dec_handle_dvd_event (dec, event)) {
          /* gst_dvd_sub_dec_advance_time (dec, dec->next_ts + GST_SECOND / 30.0); */
          gst_event_unref (event);
          ret = TRUE;
          break;
        }
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime start, duration;

      gst_event_parse_gap (event, &start, &duration);
      if (GST_CLOCK_TIME_IS_VALID (start)) {
        if (GST_CLOCK_TIME_IS_VALID (duration))
          start += duration;
        /* we do not expect another buffer until after gap,
         * so that is our position now */
        GST_DEBUG_OBJECT (dec, "Got GAP event, advancing time from %"
            GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (dec->next_ts), GST_TIME_ARGS (start));

        gst_dvd_sub_dec_advance_time (dec, start);
      } else {
        GST_WARNING_OBJECT (dec, "Got GAP event with invalid position");
      }

      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      {
#if 0
        /* Turn off forced highlight display */
        dec->forced_display = 0;
        dec->current_button = 0;
#endif
        if (dec->partialbuf) {
          gst_buffer_unmap (dec->partialbuf, &dec->partialmap);
          gst_buffer_unref (dec->partialbuf);
          dec->partialbuf = NULL;
          dec->have_title = FALSE;
        }

        if (GST_CLOCK_TIME_IS_VALID (seg.time))
          dec->next_ts = seg.time;
        else
          dec->next_ts = GST_CLOCK_TIME_NONE;

        GST_DEBUG_OBJECT (dec, "Got newsegment, new time = %"
            GST_TIME_FORMAT, GST_TIME_ARGS (dec->next_ts));

        ret = gst_pad_event_default (pad, parent, event);
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      /* Turn off forced highlight display */
      dec->forced_display = 0;
      dec->current_button = 0;

      if (dec->partialbuf) {
        gst_buffer_unmap (dec->partialbuf, &dec->partialmap);
        gst_buffer_unref (dec->partialbuf);
        dec->partialbuf = NULL;
        dec->have_title = FALSE;
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:{
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
  }
  return ret;
}

static gboolean
gst_dvd_sub_dec_handle_dvd_event (GstDvdSubDec * dec, GstEvent * event)
{
  GstStructure *structure;
  const gchar *event_name;

  structure = (GstStructure *) gst_event_get_structure (event);

  if (structure == NULL)
    goto not_handled;

  event_name = gst_structure_get_string (structure, "event");

  GST_LOG_OBJECT (dec,
      "DVD event %s with timestamp %" G_GINT64_FORMAT " on sub pad",
      GST_STR_NULL (event_name), GST_EVENT_TIMESTAMP (event));

  if (event_name == NULL)
    goto not_handled;

  if (strcmp (event_name, "dvd-spu-highlight") == 0) {
    gint button;
    gint palette, sx, sy, ex, ey;
    gint i;

    /* Details for the highlight region to display */
    if (!gst_structure_get_int (structure, "button", &button) ||
        !gst_structure_get_int (structure, "palette", &palette) ||
        !gst_structure_get_int (structure, "sx", &sx) ||
        !gst_structure_get_int (structure, "sy", &sy) ||
        !gst_structure_get_int (structure, "ex", &ex) ||
        !gst_structure_get_int (structure, "ey", &ey)) {
      GST_ERROR_OBJECT (dec, "Invalid dvd-spu-highlight event received");
      return TRUE;
    }
    dec->current_button = button;
    dec->hl_left = sx;
    dec->hl_top = sy;
    dec->hl_right = ex;
    dec->hl_bottom = ey;
    for (i = 0; i < 4; i++) {
      dec->menu_alpha[i] = ((guint32) (palette) >> (i * 4)) & 0x0f;
      dec->menu_index[i] = ((guint32) (palette) >> (16 + (i * 4))) & 0x0f;
    }

    GST_DEBUG_OBJECT (dec, "New button activated highlight=(%d,%d) to (%d,%d) "
        "palette 0x%x", sx, sy, ex, ey, palette);
    gst_setup_palette (dec);

    dec->buf_dirty = TRUE;
  } else if (strcmp (event_name, "dvd-spu-clut-change") == 0) {
    /* Take a copy of the colour table */
    gchar name[16];
    int i;
    gint value;

    GST_LOG_OBJECT (dec, "New colour table received");
    for (i = 0; i < 16; i++) {
      g_snprintf (name, sizeof (name), "clut%02d", i);
      if (!gst_structure_get_int (structure, name, &value)) {
        GST_ERROR_OBJECT (dec, "dvd-spu-clut-change event did not "
            "contain %s field", name);
        break;
      }
      dec->current_clut[i] = (guint32) (value);
    }

    gst_setup_palette (dec);

    dec->buf_dirty = TRUE;
  } else if (strcmp (event_name, "dvd-spu-stream-change") == 0
      || strcmp (event_name, "dvd-spu-reset-highlight") == 0) {
    /* Turn off forced highlight display */
    dec->current_button = 0;

    GST_LOG_OBJECT (dec, "Clearing button state");
    dec->buf_dirty = TRUE;
  } else if (strcmp (event_name, "dvd-spu-still-frame") == 0) {
    /* Handle a still frame */
    GST_LOG_OBJECT (dec, "Received still frame notification");
  } else {
    goto not_handled;
  }

  return TRUE;

not_handled:
  {
    /* Ignore all other unknown events */
    GST_LOG_OBJECT (dec, "Ignoring other custom event %" GST_PTR_FORMAT,
        structure);
    return FALSE;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dvdsubdec", GST_RANK_NONE,
          GST_TYPE_DVD_SUB_DEC) ||
      !gst_element_register (plugin, "dvdsubparse", GST_RANK_NONE,
          GST_TYPE_DVD_SUB_PARSE)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_dvd_sub_dec_debug, "dvdsubdec", 0,
      "DVD subtitle decoder");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvdsub,
    "DVD subtitle parser and decoder", plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
