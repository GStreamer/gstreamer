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


/*#define DEBUG_ENABLED */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmpeg2subt.h"
#include <string.h>

static void gst_mpeg2subt_class_init (GstMpeg2SubtClass * klass);
static void gst_mpeg2subt_base_init (GstMpeg2SubtClass * klass);
static void gst_mpeg2subt_init (GstMpeg2Subt * mpeg2subt);
static void gst_mpeg2subt_loop (GstElement * element);

static GstCaps *gst_mpeg2subt_getcaps_video (GstPad * pad);
static GstPadLinkReturn gst_mpeg2subt_link_video (GstPad * pad,
    const GstCaps * caps);
static void gst_mpeg2subt_handle_video (GstMpeg2Subt * mpeg2subt,
    GstData * _data);
static gboolean gst_mpeg2subt_src_event (GstPad * pad, GstEvent * event);
static void gst_mpeg2subt_handle_subtitle (GstMpeg2Subt * mpeg2subt,
    GstData * _data);

static void gst_mpeg2subt_merge_title (GstMpeg2Subt * mpeg2subt,
    GstBuffer * buf);
static void gst_mpeg2subt_handle_dvd_event (GstMpeg2Subt * mpeg2subt,
    GstEvent * event, gboolean from_sub_pad);
static void gst_mpeg2subt_finalize (GObject * gobject);
static void gst_mpeg2subt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeg2subt_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_setup_palette (GstMpeg2Subt * mpeg2subt, guchar * indexes,
    guchar * alpha);
static void gst_update_still_frame (GstMpeg2Subt * mpeg2subt);

/* elementfactory information */
static GstElementDetails mpeg2subt_details = {
  "MPEG2 subtitle Decoder",
  "Codec/Decoder/Video",
  "Decodes and merges MPEG2 subtitles into a video frame",
  "Wim Taymans <wim.taymans@chello.be>\n"
      "Jan Schmidt <thaytan@mad.scientist.com>"
};

static GstStaticPadTemplate video_template = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, " "format = (fourcc) { I420 }, " /* YV12 later */
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, " "format = (fourcc) { I420 }, " /* YV12 later */
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    );

static GstStaticPadTemplate subtitle_template =
GST_STATIC_PAD_TEMPLATE ("subtitle",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dvd-subpicture")
    );

GST_DEBUG_CATEGORY_STATIC (mpeg2subt_debug);
#define GST_CAT_DEFAULT (mpeg2subt_debug)

/* GstMpeg2Subt signals and args */
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

typedef struct RLE_state
{
  gint id;
  gint aligned;
  gint offset[2];
  gint clip_left;
  gint clip_right;

  guchar *target_Y;
  guchar *target_U;
  guchar *target_V;
  guchar *target_A;

  guchar next;
}
RLE_state;

static GstElementClass *parent_class = NULL;

/*static guint gst_mpeg2subt_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_mpeg2subt_get_type (void)
{
  static GType mpeg2subt_type = 0;

  if (!mpeg2subt_type) {
    static const GTypeInfo mpeg2subt_info = {
      sizeof (GstMpeg2SubtClass),
      (GBaseInitFunc) gst_mpeg2subt_base_init,
      NULL,
      (GClassInitFunc) gst_mpeg2subt_class_init,
      NULL,
      NULL,
      sizeof (GstMpeg2Subt),
      0,
      (GInstanceInitFunc) gst_mpeg2subt_init,
    };

    mpeg2subt_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMpeg2Subt",
        &mpeg2subt_info, 0);

    GST_DEBUG_CATEGORY_INIT (mpeg2subt_debug, "mpeg2subt", 0,
        "MPEG2 subtitle overlay element");
  }

  return mpeg2subt_type;
}

static void
gst_mpeg2subt_base_init (GstMpeg2SubtClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subtitle_template));

  gst_element_class_set_details (element_class, &mpeg2subt_details);
}

static void
gst_mpeg2subt_class_init (GstMpeg2SubtClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SKIP, g_param_spec_int ("skip", "skip", "skip", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));      /* CHECKME */

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mpeg2subt_set_property;
  gobject_class->get_property = gst_mpeg2subt_get_property;
  gobject_class->finalize = gst_mpeg2subt_finalize;
}

static void
gst_mpeg2subt_init (GstMpeg2Subt * mpeg2subt)
{
  mpeg2subt->videopad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&video_template), "video");
  gst_element_add_pad (GST_ELEMENT (mpeg2subt), mpeg2subt->videopad);
  gst_pad_set_link_function (mpeg2subt->videopad,
      GST_DEBUG_FUNCPTR (gst_mpeg2subt_link_video));
  gst_pad_set_getcaps_function (mpeg2subt->videopad,
      GST_DEBUG_FUNCPTR (gst_mpeg2subt_getcaps_video));

  mpeg2subt->subtitlepad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&subtitle_template), "subtitle");
  gst_element_add_pad (GST_ELEMENT (mpeg2subt), mpeg2subt->subtitlepad);

  mpeg2subt->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template), "src");
  gst_element_add_pad (GST_ELEMENT (mpeg2subt), mpeg2subt->srcpad);
  gst_pad_set_getcaps_function (mpeg2subt->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2subt_getcaps_video));
  gst_pad_set_link_function (mpeg2subt->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2subt_link_video));
  gst_pad_set_event_function (mpeg2subt->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2subt_src_event));

  gst_element_set_loop_function (GST_ELEMENT (mpeg2subt), gst_mpeg2subt_loop);
  GST_FLAG_SET (GST_ELEMENT (mpeg2subt), GST_ELEMENT_EVENT_AWARE);

  mpeg2subt->partialbuf = NULL;
  mpeg2subt->hold_frame = NULL;
  mpeg2subt->still_frame = NULL;
  mpeg2subt->have_title = FALSE;
  mpeg2subt->start_display_time = GST_CLOCK_TIME_NONE;
  mpeg2subt->end_display_time = GST_CLOCK_TIME_NONE;
  mpeg2subt->forced_display = FALSE;
  memset (mpeg2subt->current_clut, 0, 16 * sizeof (guint32));
  memset (mpeg2subt->subtitle_index, 0, sizeof (mpeg2subt->subtitle_index));
  memset (mpeg2subt->menu_index, 0, sizeof (mpeg2subt->menu_index));
  memset (mpeg2subt->subtitle_alpha, 0, sizeof (mpeg2subt->subtitle_alpha));
  memset (mpeg2subt->menu_alpha, 0, sizeof (mpeg2subt->menu_alpha));
  memset (mpeg2subt->out_buffers, 0, sizeof (mpeg2subt->out_buffers));
  mpeg2subt->pending_video_buffer = NULL;
  mpeg2subt->next_video_time = GST_CLOCK_TIME_NONE;
  mpeg2subt->pending_subtitle_buffer = NULL;
  mpeg2subt->next_subtitle_time = GST_CLOCK_TIME_NONE;
}

static void
gst_mpeg2subt_finalize (GObject * gobject)
{
  GstMpeg2Subt *mpeg2subt = GST_MPEG2SUBT (gobject);
  gint i;

  for (i = 0; i < 3; i++) {
    if (mpeg2subt->out_buffers[i])
      g_free (mpeg2subt->out_buffers[i]);
  }

  if (mpeg2subt->partialbuf)
    gst_buffer_unref (mpeg2subt->partialbuf);
}

static GstCaps *
gst_mpeg2subt_getcaps_video (GstPad * pad)
{
  GstMpeg2Subt *mpeg2subt = GST_MPEG2SUBT (gst_pad_get_parent (pad));
  GstPad *otherpad;

  otherpad =
      (pad == mpeg2subt->srcpad) ? mpeg2subt->videopad : mpeg2subt->srcpad;

  return gst_pad_get_allowed_caps (otherpad);
}

static GstPadLinkReturn
gst_mpeg2subt_link_video (GstPad * pad, const GstCaps * caps)
{
  GstMpeg2Subt *mpeg2subt = GST_MPEG2SUBT (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstPadLinkReturn ret;
  GstStructure *structure;
  gint width, height;
  gint i;

  otherpad =
      (pad == mpeg2subt->srcpad) ? mpeg2subt->videopad : mpeg2subt->srcpad;

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_FAILED (ret)) {
    return ret;
  }

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    return GST_PAD_LINK_REFUSED;
  }

  mpeg2subt->in_width = width;
  mpeg2subt->in_height = height;

  /* Allocate compositing buffers */
  for (i = 0; i < 3; i++) {
    if (mpeg2subt->out_buffers[i])
      g_free (mpeg2subt->out_buffers[i]);
    mpeg2subt->out_buffers[i] = g_malloc (sizeof (guint16) * width);
  }

  return GST_PAD_LINK_OK;
}

static void
gst_mpeg2subt_handle_video (GstMpeg2Subt * mpeg2subt, GstData * _data)
{
  if (GST_IS_BUFFER (_data)) {
    GstBuffer *buf = GST_BUFFER (_data);
    guchar *data;
    glong size;

    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    if (mpeg2subt->still_frame) {
      gst_buffer_unref (mpeg2subt->still_frame);
      mpeg2subt->still_frame = NULL;
    }

    if (!mpeg2subt->hold_frame) {
      mpeg2subt->hold_frame = buf;
    } else {
      GstBuffer *out_buf;

      out_buf = mpeg2subt->hold_frame;
      mpeg2subt->hold_frame = buf;

      if (mpeg2subt->have_title) {
        if ((mpeg2subt->forced_display && (mpeg2subt->current_button != 0))
            ||
            ((mpeg2subt->start_display_time <= GST_BUFFER_TIMESTAMP (out_buf))
                && (mpeg2subt->end_display_time >=
                    GST_BUFFER_TIMESTAMP (out_buf)))) {
          out_buf = gst_buffer_copy_on_write (out_buf);
          gst_mpeg2subt_merge_title (mpeg2subt, out_buf);
        }
      }

      gst_pad_push (mpeg2subt->srcpad, GST_DATA (out_buf));
    }
  } else if (GST_IS_EVENT (_data)) {
    switch (GST_EVENT_TYPE (GST_EVENT (_data))) {
      case GST_EVENT_ANY:
        gst_mpeg2subt_handle_dvd_event (mpeg2subt, GST_EVENT (_data), FALSE);
        gst_data_unref (_data);
        break;
      case GST_EVENT_DISCONTINUOUS:
        /* Turn off forced highlight display */
        mpeg2subt->forced_display = 0;
        if (mpeg2subt->still_frame) {
          gst_buffer_unref (mpeg2subt->still_frame);
          mpeg2subt->still_frame = NULL;
        }
        if (mpeg2subt->hold_frame) {
          gst_buffer_unref (mpeg2subt->hold_frame);
          mpeg2subt->hold_frame = NULL;
        }
        gst_pad_push (mpeg2subt->srcpad, _data);
        break;
      default:
        gst_pad_push (mpeg2subt->srcpad, _data);
        break;
    }
  } else
    gst_data_unref (_data);
}

static gboolean
gst_mpeg2subt_src_event (GstPad * pad, GstEvent * event)
{
  GstMpeg2Subt *mpeg2subt = GST_MPEG2SUBT (gst_pad_get_parent (pad));

  return gst_pad_send_event (GST_PAD_PEER (mpeg2subt->videopad), event);
}

static void
gst_mpeg2subt_parse_header (GstMpeg2Subt * mpeg2subt)
{
#define PARSE_BYTES_NEEDED(x) if ((buf+(x)) >= end) \
  { GST_WARNING("Subtitle stream broken parsing %d", *buf); \
    broken = TRUE; break; }

  guchar *buf;
  guchar *start = GST_BUFFER_DATA (mpeg2subt->partialbuf);
  guchar *end;
  gboolean broken = FALSE;
  gboolean last_seq = FALSE;
  guchar *next_seq = NULL;
  guint event_time;

  mpeg2subt->forced_display = FALSE;
  g_return_if_fail (mpeg2subt->packet_size >= 4);

  buf = start + mpeg2subt->data_size;
  end = buf + mpeg2subt->packet_size;
  event_time = GUINT16_FROM_BE (*(guint16 *) (buf));
  next_seq = start + GUINT16_FROM_BE (*(guint16 *) (buf + 2));
  /* If the next control sequence is at the current offset, this is 
   * the last one */
  last_seq = (next_seq == buf);
  buf += 4;

  while ((buf < end) && (!broken)) {
    switch (*buf) {
      case SPU_FORCE_DISPLAY:  /* Forced display menu subtitle */
        mpeg2subt->forced_display = TRUE;
        buf++;
        break;
      case SPU_SHOW:           /* Show the subtitle in this packet */
        mpeg2subt->start_display_time =
            GST_BUFFER_TIMESTAMP (mpeg2subt->partialbuf) +
            ((GST_SECOND * event_time) / 90);
        GST_DEBUG ("Subtitle starts at %" G_GUINT64_FORMAT,
            mpeg2subt->end_display_time);
        buf++;
        break;
      case SPU_HIDE:           /* 02 ff (ff) is the end of the packet, hide the  */
        mpeg2subt->end_display_time =
            GST_BUFFER_TIMESTAMP (mpeg2subt->partialbuf) +
            ((GST_SECOND * event_time) / 90);
        GST_DEBUG ("Subtitle ends at %" G_GUINT64_FORMAT,
            mpeg2subt->end_display_time);
        buf++;
        break;
      case SPU_SET_PALETTE:    /* palette */
        PARSE_BYTES_NEEDED (3);

        mpeg2subt->subtitle_index[3] = buf[1] >> 4;
        mpeg2subt->subtitle_index[2] = buf[1] & 0xf;
        mpeg2subt->subtitle_index[1] = buf[2] >> 4;
        mpeg2subt->subtitle_index[0] = buf[2] & 0xf;
        buf += 3;
        break;
      case SPU_SET_ALPHA:      /* transparency palette */
        PARSE_BYTES_NEEDED (3);

        mpeg2subt->subtitle_alpha[3] = buf[1] >> 4;
        mpeg2subt->subtitle_alpha[2] = buf[1] & 0xf;
        mpeg2subt->subtitle_alpha[1] = buf[2] >> 4;
        mpeg2subt->subtitle_alpha[0] = buf[2] & 0xf;
        buf += 3;
        break;
      case SPU_SET_SIZE:       /* image coordinates */
        PARSE_BYTES_NEEDED (7);

        mpeg2subt->left =
            CLAMP ((((unsigned int) buf[1]) << 4) | (buf[2] >> 4), 0,
            (mpeg2subt->in_width - 1));
        mpeg2subt->top =
            CLAMP ((((unsigned int) buf[4]) << 4) | (buf[5] >> 4), 0,
            (mpeg2subt->in_height - 1));
        mpeg2subt->right =
            CLAMP ((((buf[2] & 0x0f) << 8) | buf[3]), 0,
            (mpeg2subt->in_width - 1));
        mpeg2subt->bottom =
            CLAMP ((((buf[5] & 0x0f) << 8) | buf[6]), 0,
            (mpeg2subt->in_height - 1));

        GST_DEBUG ("left %d, top %d, right %d, bottom %d", mpeg2subt->left,
            mpeg2subt->top, mpeg2subt->right, mpeg2subt->bottom);
        buf += 7;
        break;
      case SPU_SET_OFFSETS:    /* image 1 / image 2 offsets */
        PARSE_BYTES_NEEDED (5);
        mpeg2subt->offset[0] = (((unsigned int) buf[1]) << 8) | buf[2];
        mpeg2subt->offset[1] = (((unsigned int) buf[3]) << 8) | buf[4];
        GST_DEBUG ("Offset1 %d, Offset2 %d", mpeg2subt->offset[0],
            mpeg2subt->offset[1]);
        buf += 5;
        break;
      case SPU_WIPE:
      {
        guint length;

        GST_WARNING ("SPU_WIPE not yet implemented");
        PARSE_BYTES_NEEDED (3);

        length = (buf[1] << 8) | (buf[2]);
        buf += 1 + length;
      }
        break;
      case SPU_END:
        buf = (last_seq) ? end : next_seq;

        /* Start a new control sequence */
        if (buf + 4 < end) {
          event_time = GUINT16_FROM_BE (*(guint16 *) (buf));
          next_seq = start + GUINT16_FROM_BE (*(guint16 *) (buf + 2));
          last_seq = (next_seq == buf);
        }
        buf += 4;
        break;
      default:
        GST_ERROR
            ("Invalid sequence in subtitle packet header (%.2x). Skipping",
            *buf);
        broken = TRUE;
        break;
    }
  }

  if (!mpeg2subt->forced_display)
    gst_setup_palette (mpeg2subt, mpeg2subt->subtitle_index,
        mpeg2subt->subtitle_alpha);
}

inline int
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

/* Premultiply the current lookup table into the palette_cache */
static void
gst_setup_palette (GstMpeg2Subt * mpeg2subt, guchar * indexes, guchar * alpha)
{
  gint i;
  YUVA_val *target = mpeg2subt->palette_cache;

  for (i = 0; i < 4; i++, target++) {
    guint32 col = mpeg2subt->current_clut[indexes[i]];

    target->Y = (guint16) ((col >> 16) & 0xff) * alpha[i];
    target->U = (guint16) ((col >> 8) & 0xff) * alpha[i];
    target->V = (guint16) (col & 0xff) * alpha[i];
    target->A = alpha[i];
  }
}

inline guint
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

/* 
 * This function steps over each run-length segment, drawing 
 * into the YUVA buffers as it goes. UV are composited and then output
 * at half width/height
 */
static void
gst_draw_rle_line (GstMpeg2Subt * mpeg2subt, guchar * buffer, RLE_state * state)
{
  gint length, colourid;
  gint right = mpeg2subt->right + 1;
  YUVA_val *colour_entry;
  guint code;
  gint x;
  gboolean in_clip = FALSE;
  guchar *target_Y;
  guint16 *target_U;
  guint16 *target_V;
  guint16 *target_A;

  target_Y = state->target_Y;
  target_U = mpeg2subt->out_buffers[0];
  target_V = mpeg2subt->out_buffers[1];
  target_A = mpeg2subt->out_buffers[2];
  x = mpeg2subt->left;
  while (x < right) {
    code = gst_get_rle_code (buffer, state);
    length = code >> 2;
    colourid = code & 3;
    colour_entry = mpeg2subt->palette_cache + colourid;

    /* Length = 0 implies fill to the end of the line */
    if (length == 0)
      length = right - x;
    else {
      /* Restrict the colour run to the end of the line */
      length = length < (right - x) ? length : (right - x);
    }

    /* Check if this run of colour crosses into the clip region */
    in_clip = (((x + length) >= state->clip_left) && (x <= state->clip_right));

    /* Draw YA onto the frame via target_Y, UVA into the composite buffers */
    if ((in_clip) && (colour_entry->A)) {
      guint16 inv_alpha = 0xf - colour_entry->A;
      gint i;

      for (i = 0; i < length; i++) {
        *target_Y = ((inv_alpha * (*target_Y)) + colour_entry->Y) / 0xf;
        *target_U += colour_entry->U;
        *target_V += colour_entry->V;
        *target_A += colour_entry->A;
        target_Y++;
        target_U++;
        target_V++;
        target_A++;
      }
    } else {
      target_Y += length;
      target_U += length;
      target_V += length;
      target_A += length;
    }
    x += length;
  }
}

inline void
gst_merge_uv_data (GstMpeg2Subt * mpeg2subt, guchar * buffer, RLE_state * state)
{
  gint x;
  guchar *target_V;
  guchar *target_U;
  gint width = mpeg2subt->right - mpeg2subt->left + 1;

  guint16 *comp_U;
  guint16 *comp_V;
  guint16 *comp_A;

  /* The compositing buffers should contain the results of accumulating 2 scanlines of 
   * U, V (premultiplied) and A data. Merge them back into their output buffers at 
   * half width/height.
   */
  target_U = state->target_U;
  target_V = state->target_V;
  comp_U = mpeg2subt->out_buffers[0];
  comp_V = mpeg2subt->out_buffers[1];
  comp_A = mpeg2subt->out_buffers[2];

  for (x = 0; x < width; x += 2) {
    guint16 temp1, temp2;

    /* Average out the alpha accumulated to compute transparency */
    guint16 alpha = (comp_A[0] + comp_A[1]);

    if (alpha > 0) {
      temp1 = (*target_U) * ((4 * 0xf) - alpha) + comp_U[0] + comp_U[1];
      temp2 = (*target_V) * ((4 * 0xf) - alpha) + comp_V[0] + comp_V[1];
      *target_U = temp1 / (4 * 0xf);
      *target_V = temp2 / (4 * 0xf);
    };
    comp_U += 2;
    comp_V += 2;
    comp_A += 2;
    target_U++;
    target_V++;
  }
}

/*
 * Decode the RLE subtitle image and blend with the current
 * frame buffer.
 */
static void
gst_mpeg2subt_merge_title (GstMpeg2Subt * mpeg2subt, GstBuffer * buf)
{
  gint y;
  gint width = mpeg2subt->right - mpeg2subt->left + 1;
  gint Y_stride;
  gint UV_stride;

  guchar *buffer = GST_BUFFER_DATA (mpeg2subt->partialbuf);
  gint last_y;
  gint first_y;
  RLE_state state;

  /* Set up the initial offsets, remembering the half-res size for UV in I420 packing
   * see http://www.fourcc.org for details
   */
  Y_stride = mpeg2subt->in_width;
  UV_stride = (mpeg2subt->in_width + 1) / 2;

  GST_DEBUG ("Merging subtitle on frame at time %" G_GUINT64_FORMAT
      " using %s colour table", GST_BUFFER_TIMESTAMP (buf),
      mpeg2subt->forced_display ? "menu" : "subtitle");

  state.id = 0;
  state.aligned = 1;
  state.offset[0] = mpeg2subt->offset[0];
  state.offset[1] = mpeg2subt->offset[1];

  /* skip over lines until we hit the clip region */
  if (mpeg2subt->forced_display) {
    state.clip_right = mpeg2subt->clip_right;
    state.clip_left = mpeg2subt->clip_left;
    last_y = mpeg2subt->clip_bottom;
    first_y = mpeg2subt->clip_top;
  } else {
    state.clip_right = mpeg2subt->right;
    state.clip_left = mpeg2subt->left;
    last_y = mpeg2subt->bottom;
    first_y = mpeg2subt->top;
  }

  for (y = mpeg2subt->top; y < first_y; y++) {
    /* Skip a line of RLE data */
    gint length;
    guint code;
    gint x = 0;

    while (x < width) {
      code = gst_get_rle_code (buffer, &state);
      length = code >> 2;

      /* Length = 0 implies fill to the end of the line so we're done */
      if (length == 0)
        break;

      x += length;
    }
    if (!state.aligned)
      gst_get_nibble (buffer, &state);
    state.id = !state.id;
  }

  state.target_Y = GST_BUFFER_DATA (buf) + mpeg2subt->left + (y * Y_stride);
  state.target_V = GST_BUFFER_DATA (buf) + (Y_stride * mpeg2subt->in_height)
      + ((mpeg2subt->left) / 2) + ((y / 2) * UV_stride);
  state.target_U =
      state.target_V + UV_stride * ((mpeg2subt->in_height + 1) / 2);

  memset (mpeg2subt->out_buffers[0], 0, sizeof (guint16) * Y_stride);
  memset (mpeg2subt->out_buffers[1], 0, sizeof (guint16) * Y_stride);
  memset (mpeg2subt->out_buffers[2], 0, sizeof (guint16) * Y_stride);

  /* Now draw scanlines until we hit last_y or end of RLE data */
  for (; ((state.offset[1] < mpeg2subt->data_size + 2) && (y <= last_y)); y++) {
    gst_draw_rle_line (mpeg2subt, buffer, &state);
    if (state.id) {
      gst_merge_uv_data (mpeg2subt, buffer, &state);

      /* Clear the compositing buffers */
      memset (mpeg2subt->out_buffers[0], 0, sizeof (guint16) * Y_stride);
      memset (mpeg2subt->out_buffers[1], 0, sizeof (guint16) * Y_stride);
      memset (mpeg2subt->out_buffers[2], 0, sizeof (guint16) * Y_stride);

      state.target_U += UV_stride;
      state.target_V += UV_stride;
    }
    state.target_Y += Y_stride;

    /* Realign the RLE state for the next line */
    if (!state.aligned)
      gst_get_nibble (buffer, &state);
    state.id = !state.id;
  }
}

static void
gst_update_still_frame (GstMpeg2Subt * mpeg2subt)
{
  GstBuffer *out_buf;

  if ((mpeg2subt->still_frame) &&
      (mpeg2subt->have_title) &&
      ((mpeg2subt->forced_display && (mpeg2subt->current_button != 0)))) {
    gst_buffer_ref (mpeg2subt->still_frame);
    out_buf = gst_buffer_copy_on_write (mpeg2subt->still_frame);
    gst_mpeg2subt_merge_title (mpeg2subt, out_buf);
    gst_pad_push (mpeg2subt->srcpad, GST_DATA (out_buf));
  }
}

static void
gst_mpeg2subt_handle_subtitle (GstMpeg2Subt * mpeg2subt, GstData * _data)
{
  g_return_if_fail (_data != NULL);

  if (GST_IS_BUFFER (_data)) {
    GstBuffer *buf = GST_BUFFER (_data);
    guchar *data;
    glong size = 0;

    if (mpeg2subt->have_title) {
      gst_buffer_unref (mpeg2subt->partialbuf);
      mpeg2subt->partialbuf = NULL;
      mpeg2subt->have_title = FALSE;
    }

    GST_DEBUG ("Got subtitle buffer, pts %" G_GUINT64_FORMAT,
        GST_BUFFER_TIMESTAMP (buf));

    /* deal with partial frame from previous buffer */
    if (mpeg2subt->partialbuf) {
      GstBuffer *merge;

      merge = gst_buffer_merge (mpeg2subt->partialbuf, buf);
      gst_buffer_unref (mpeg2subt->partialbuf);
      gst_buffer_unref (buf);
      mpeg2subt->partialbuf = merge;
    } else {
      mpeg2subt->partialbuf = buf;
    }

    data = GST_BUFFER_DATA (mpeg2subt->partialbuf);
    size = GST_BUFFER_SIZE (mpeg2subt->partialbuf);

    if (size > 4) {
      mpeg2subt->packet_size = GST_READ_UINT16_BE (data);

      if (mpeg2subt->packet_size == size) {
        GST_LOG ("Subtitle packet size %d, current size %ld",
            mpeg2subt->packet_size, size);

        mpeg2subt->data_size = GST_READ_UINT16_BE (data + 2);
        mpeg2subt->have_title = TRUE;

        gst_mpeg2subt_parse_header (mpeg2subt);
      }
    }
  } else if (GST_IS_EVENT (_data)) {
    switch (GST_EVENT_TYPE (GST_EVENT (_data))) {
      case GST_EVENT_ANY:
        GST_LOG ("DVD event on subtitle pad with timestamp %llu",
            GST_EVENT_TIMESTAMP (GST_EVENT (_data)));
        gst_mpeg2subt_handle_dvd_event (mpeg2subt, GST_EVENT (_data), TRUE);
        break;
      case GST_EVENT_EMPTY:
        if (GST_CLOCK_TIME_IS_VALID (mpeg2subt->next_video_time) &&
            (mpeg2subt->next_video_time > 0)) {
          mpeg2subt->next_subtitle_time = mpeg2subt->next_video_time + 1;
          GST_LOG ("Forwarding subtitle time to %llu",
              mpeg2subt->next_subtitle_time);
        }
        gst_update_still_frame (mpeg2subt);
        break;
      default:
        GST_LOG ("Got event of type %d on subtitle pad",
            GST_EVENT_TYPE (GST_EVENT (_data)));
        break;
    }
    gst_data_unref (_data);
  } else
    gst_data_unref (_data);
}

static void
gst_mpeg2subt_handle_dvd_event (GstMpeg2Subt * mpeg2subt, GstEvent * event,
    gboolean from_sub_pad)
{
  GstStructure *structure;
  const gchar *event_type;

  structure = event->event_data.structure.structure;

  event_type = gst_structure_get_string (structure, "event");
  g_return_if_fail (event_type != NULL);

  if (from_sub_pad && !strcmp (event_type, "dvd-spu-highlight")) {
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
      GST_ERROR ("Invalid dvd-spu-highlight event received");
      return;
    }
    mpeg2subt->current_button = button;
    mpeg2subt->clip_left = sx;
    mpeg2subt->clip_top = sy;
    mpeg2subt->clip_right = ex;
    mpeg2subt->clip_bottom = ey;
    for (i = 0; i < 4; i++) {
      mpeg2subt->menu_alpha[i] = ((guint32) (palette) >> (i * 4)) & 0x0f;
      mpeg2subt->menu_index[i] = ((guint32) (palette) >> (16 + (i * 4))) & 0x0f;
    }

    GST_DEBUG ("New button activated clip=(%d,%d) to (%d,%d) palette 0x%x", sx,
        sy, ex, ey, palette);
    gst_setup_palette (mpeg2subt, mpeg2subt->menu_index, mpeg2subt->menu_alpha);

    gst_update_still_frame (mpeg2subt);
  } else if (from_sub_pad && !strcmp (event_type, "dvd-spu-clut-change")) {
    /* Take a copy of the colour table */
    guchar name[16];
    int i;
    gint value;

    GST_LOG ("New colour table recieved");
    for (i = 0; i < 16; i++) {
      sprintf (name, "clut%02d", i);
      if (!gst_structure_get_int (structure, name, &value)) {
        GST_ERROR ("dvd-spu-clut-change event did not contain %s field", name);
        break;
      }
      mpeg2subt->current_clut[i] = (guint32) (value);
    }

    if (mpeg2subt->forced_display)
      gst_setup_palette (mpeg2subt, mpeg2subt->menu_index,
          mpeg2subt->menu_alpha);
    else
      gst_setup_palette (mpeg2subt, mpeg2subt->subtitle_index,
          mpeg2subt->subtitle_alpha);

    gst_update_still_frame (mpeg2subt);
  } else if ((from_sub_pad && !strcmp (event_type, "dvd-spu-stream-change"))
      || (from_sub_pad && !strcmp (event_type, "dvd-spu-reset-highlight"))) {
    /* Turn off forced highlight display */
    mpeg2subt->current_button = 0;
    mpeg2subt->clip_left = mpeg2subt->left;
    mpeg2subt->clip_top = mpeg2subt->top;
    mpeg2subt->clip_right = mpeg2subt->right;
    mpeg2subt->clip_bottom = mpeg2subt->bottom;
    GST_LOG ("Clearing button state");
    gst_update_still_frame (mpeg2subt);
  } else if (!from_sub_pad && !strcmp (event_type, "dvd-spu-still-frame")) {
    /* Handle a still frame */
    GST_LOG ("Received still frame notification");
    if (mpeg2subt->still_frame)
      gst_buffer_unref (mpeg2subt->still_frame);
    mpeg2subt->still_frame = mpeg2subt->hold_frame;
    mpeg2subt->hold_frame = NULL;
    gst_update_still_frame (mpeg2subt);
  } else {
    /* Ignore all other unknown events */
    GST_LOG ("Ignoring DVD event %s from %s pad", event_type,
        from_sub_pad ? "sub" : "video");
  }
}

static void
gst_mpeg2subt_loop (GstElement * element)
{
  GstMpeg2Subt *mpeg2subt = GST_MPEG2SUBT (element);
  GstData *data;
  GstClockTime timestamp = 0;

  /* Process any pending video buffer */
  if (mpeg2subt->pending_video_buffer) {
    gst_mpeg2subt_handle_video (mpeg2subt, mpeg2subt->pending_video_buffer);
    mpeg2subt->pending_video_buffer = NULL;
  }
  data = mpeg2subt->pending_video_buffer = gst_pad_pull (mpeg2subt->videopad);
  if (!data)
    return;

  if (GST_IS_BUFFER (data)) {
    timestamp = GST_BUFFER_TIMESTAMP (GST_BUFFER (data));
  } else if (GST_IS_EVENT (data)) {
    timestamp = GST_EVENT_TIMESTAMP (GST_EVENT (data));
  } else {
    GST_WARNING ("Got GstData of unknown type %d", GST_DATA_TYPE (data));
  }
  if (timestamp && GST_CLOCK_TIME_IS_VALID (timestamp) && (timestamp > 0)) {
    mpeg2subt->next_video_time = timestamp;
    GST_LOG ("next_video_time = %llu, next_subtitle_time = %llu",
        mpeg2subt->next_video_time, mpeg2subt->next_subtitle_time);
  }

  /* Process subtitle buffers until we get one beyond 'next_video_time' */
  if (mpeg2subt->pending_subtitle_buffer) {
    gst_mpeg2subt_handle_subtitle (mpeg2subt,
        mpeg2subt->pending_subtitle_buffer);
    mpeg2subt->pending_subtitle_buffer = NULL;
  }
  data = mpeg2subt->pending_subtitle_buffer =
      gst_pad_pull (mpeg2subt->subtitlepad);
  if (!data) {
    return;
  }

  if (GST_IS_BUFFER (data)) {
    timestamp = GST_BUFFER_TIMESTAMP (GST_BUFFER (data));
  } else if (GST_IS_EVENT (data)) {
    timestamp = GST_EVENT_TIMESTAMP (GST_EVENT (data));
  } else {
    GST_WARNING ("Got GstData of unknown type %d", GST_DATA_TYPE (data));
  }
  if (GST_CLOCK_TIME_IS_VALID (timestamp) && (timestamp > 0)) {
    mpeg2subt->next_subtitle_time = timestamp;
    GST_LOG ("next_subtitle_time = %llu, next_video_time = %llu",
        mpeg2subt->next_subtitle_time, mpeg2subt->next_video_time);
  }
}

static void
gst_mpeg2subt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeg2Subt *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MPEG2SUBT (object));
  src = GST_MPEG2SUBT (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mpeg2subt_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMpeg2Subt *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MPEG2SUBT (object));
  src = GST_MPEG2SUBT (object);

  switch (prop_id) {
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mpeg2subt",
      GST_RANK_NONE, GST_TYPE_MPEG2SUBT);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg2sub",
    "MPEG-2 video subtitle parser",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
