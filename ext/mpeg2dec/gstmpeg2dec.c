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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <inttypes.h>

#include "gstmpeg2dec.h"

/* mpeg2dec changed a struct name after 0.3.1, here's a workaround */
/* mpeg2dec also only defined MPEG2_RELEASE after 0.3.1
   #if MPEG2_RELEASE < MPEG2_VERSION(0,3,2)
*/
#ifndef MPEG2_RELEASE
#define MPEG2_VERSION(a,b,c) ((((a)&0xff)<<16)|(((b)&0xff)<<8)|((c)&0xff))
#define MPEG2_RELEASE MPEG2_VERSION(0,3,1)
typedef picture_t mpeg2_picture_t;
typedef gint mpeg2_state_t;

#define STATE_BUFFER 0
#endif

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_SEEK);
GST_DEBUG_CATEGORY_STATIC (mpeg2dec_debug);
#define GST_CAT_DEFAULT (mpeg2dec_debug)

/* table with framerates expressed as fractions */
static gdouble fpss[] = { 24.0 / 1.001, 24.0, 25.0,
  30.0 / 1.001, 30.0, 50.0,
  60.0 / 1.001, 60.0, 0
};

/* frame periods */
static guint frame_periods[] = {
  1126125, 1125000, 1080000, 900900, 900000, 540000, 450450, 450000, 0
};

/* elementfactory information */
static GstElementDetails gst_mpeg2dec_details = {
  "mpeg1 and mpeg2 video decoder",
  "Codec/Decoder/Video",
  "Uses libmpeg2 to decode MPEG video streams",
  "Wim Taymans <wim.taymans@chello.be>",
};

/* Mpeg2dec signals and args */
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

/*
 * We can't use fractions in static pad templates, so
 * we do something manual...
 */
static GstPadTemplate *
src_templ (void)
{
  static GstPadTemplate *templ = NULL;

  if (!templ) {
    GstCaps *caps;
    GstStructure *structure;
    GValue list = { 0 }
    , fps = {
    0}
    , fmt = {
    0};
    char *fmts[] = { "YV12", "I420", "Y42B", NULL };
    guint n;

    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC,
        GST_MAKE_FOURCC ('I', '4', '2', '0'),
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);

    structure = gst_caps_get_structure (caps, 0);

    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&fps, G_TYPE_DOUBLE);
    for (n = 0; fpss[n] != 0; n++) {
      g_value_set_double (&fps, fpss[n]);
      gst_value_list_append_value (&list, &fps);
    }
    gst_structure_set_value (structure, "framerate", &list);
    g_value_unset (&list);
    g_value_unset (&fps);

    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&fmt, GST_TYPE_FOURCC);
    for (n = 0; fmts[n] != NULL; n++) {
      gst_value_set_fourcc (&fmt, GST_STR_FOURCC (fmts[n]));
      gst_value_list_append_value (&list, &fmt);
    }
    gst_structure_set_value (structure, "format", &list);
    g_value_unset (&list);
    g_value_unset (&fmt);

    templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

#ifdef enable_user_data
static GstStaticPadTemplate user_data_template_factory =
GST_STATIC_PAD_TEMPLATE ("user_data",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
#endif

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) false")
    );

static void gst_mpeg2dec_base_init (gpointer g_class);
static void gst_mpeg2dec_class_init (GstMpeg2decClass * klass);
static void gst_mpeg2dec_init (GstMpeg2dec * mpeg2dec);

static void gst_mpeg2dec_dispose (GObject * object);
static void gst_mpeg2dec_reset (GstMpeg2dec * mpeg2dec);

static void gst_mpeg2dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeg2dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mpeg2dec_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_mpeg2dec_get_index (GstElement * element);

static gboolean gst_mpeg2dec_src_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_mpeg2dec_get_src_query_types (GstPad * pad);

static gboolean gst_mpeg2dec_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_mpeg2dec_sink_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_mpeg2dec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static GstElementStateReturn gst_mpeg2dec_change_state (GstElement * element);

static gboolean gst_mpeg2dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mpeg2dec_chain (GstPad * pad, GstBuffer * buf);

//static gboolean gst_mpeg2dec_sink_query (GstPad * pad, GstQuery * query);
static GstCaps *gst_mpeg2dec_src_getcaps (GstPad * pad);

#if 0
static const GstFormat *gst_mpeg2dec_get_formats (GstPad * pad);
#endif

#if 0
static const GstEventMask *gst_mpeg2dec_get_event_masks (GstPad * pad);
#endif

static GstElementClass *parent_class = NULL;

static GstBuffer *crop_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * input);
static gboolean check_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * buf);
static gboolean free_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * buf);
static void free_all_buffers (GstMpeg2dec * mpeg2dec);

/*static guint gst_mpeg2dec_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_mpeg2dec_get_type (void)
{
  static GType mpeg2dec_type = 0;

  if (!mpeg2dec_type) {
    static const GTypeInfo mpeg2dec_info = {
      sizeof (GstMpeg2decClass),
      gst_mpeg2dec_base_init,
      NULL,
      (GClassInitFunc) gst_mpeg2dec_class_init,
      NULL,
      NULL,
      sizeof (GstMpeg2dec),
      0,
      (GInstanceInitFunc) gst_mpeg2dec_init,
    };

    mpeg2dec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMpeg2dec", &mpeg2dec_info,
        0);
  }

  GST_DEBUG_CATEGORY_INIT (mpeg2dec_debug, "mpeg2dec", 0,
      "MPEG2 decoder element");

  return mpeg2dec_type;
}

static void
gst_mpeg2dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, src_templ ());
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
#ifdef enable_user_data
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&user_data_template_factory));
#endif
  gst_element_class_set_details (element_class, &gst_mpeg2dec_details);
}

static void
gst_mpeg2dec_class_init (GstMpeg2decClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mpeg2dec_set_property;
  gobject_class->get_property = gst_mpeg2dec_get_property;
  gobject_class->dispose = gst_mpeg2dec_dispose;

  gstelement_class->change_state = gst_mpeg2dec_change_state;
  gstelement_class->set_index = gst_mpeg2dec_set_index;
  gstelement_class->get_index = gst_mpeg2dec_get_index;
}

static void
gst_mpeg2dec_init (GstMpeg2dec * mpeg2dec)
{
  int i;

  /* create the sink and src pads */
  mpeg2dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->sinkpad);
  gst_pad_set_chain_function (mpeg2dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_chain));
  //gst_pad_set_query_function (mpeg2dec->sinkpad,
  //                            GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_sink_query));
  gst_pad_set_event_function (mpeg2dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_sink_event));

  mpeg2dec->srcpad = gst_pad_new_from_template (src_templ (), "src");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->srcpad);
  gst_pad_set_getcaps_function (mpeg2dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_getcaps));
  gst_pad_set_event_function (mpeg2dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_event));
  gst_pad_set_query_type_function (mpeg2dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_src_query_types));
  gst_pad_set_query_function (mpeg2dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_query));

#ifdef enable_user_data
  mpeg2dec->userdatapad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&user_data_template_factory), "user_data");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->userdatapad);
#endif

  /* initialize the mpeg2dec acceleration */
  mpeg2_accel (MPEG2_ACCEL_DETECT);
  mpeg2dec->closed = TRUE;
  mpeg2dec->have_fbuf = FALSE;
  mpeg2dec->offset = 0;

  for (i = 0; i < GST_MPEG2DEC_NUM_BUFS; i++)
    mpeg2dec->buffers[i] = NULL;

}

static void
gst_mpeg2dec_close_decoder (GstMpeg2dec * mpeg2dec)
{
  if (!mpeg2dec->closed) {
    mpeg2_close (mpeg2dec->decoder);
    free_all_buffers (mpeg2dec);
    mpeg2dec->closed = TRUE;
    mpeg2dec->decoder = NULL;
  }
}

static void
gst_mpeg2dec_open_decoder (GstMpeg2dec * mpeg2dec)
{
  gst_mpeg2dec_close_decoder (mpeg2dec);
  mpeg2dec->decoder = mpeg2_init ();
  mpeg2dec->closed = FALSE;
  mpeg2dec->have_fbuf = FALSE;
  mpeg2_custom_fbuf (mpeg2dec->decoder, 1);
}

static void
gst_mpeg2dec_dispose (GObject * object)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (object);

  gst_mpeg2dec_close_decoder (mpeg2dec);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mpeg2dec_reset (GstMpeg2dec * mpeg2dec)
{
  /* reset the initial video state */
  mpeg2dec->format = MPEG2DEC_FORMAT_NONE;
  mpeg2dec->width = -1;
  mpeg2dec->height = -1;
  mpeg2dec->segment_start = 0;
  mpeg2dec->segment_end = -1;
  mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
  mpeg2dec->frame_period = 0;
  gst_pad_use_fixed_caps (mpeg2dec->srcpad);
  gst_mpeg2dec_open_decoder (mpeg2dec);
  mpeg2dec->need_sequence = TRUE;
}
static void
gst_mpeg2dec_set_index (GstElement * element, GstIndex * index)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  mpeg2dec->index = index;

  gst_index_get_writer_id (index, GST_OBJECT (element), &mpeg2dec->index_id);
}

static GstIndex *
gst_mpeg2dec_get_index (GstElement * element)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  return mpeg2dec->index;
}

static gboolean
put_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * buf)
{
  int i;

  for (i = 0; i < GST_MPEG2DEC_NUM_BUFS; i++) {
    if (mpeg2dec->buffers[i] == NULL) {
      GST_DEBUG_OBJECT (mpeg2dec, "Placing %p at slot %d", buf, i);
      mpeg2dec->buffers[i] = buf;
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
check_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * buf)
{
  int i;

  for (i = 0; i < GST_MPEG2DEC_NUM_BUFS; i++)
    if (mpeg2dec->buffers[i] == buf)
      return TRUE;

  return FALSE;
}

static gboolean
free_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * buf)
{
  int i;

  for (i = 0; i < GST_MPEG2DEC_NUM_BUFS; i++) {
    if (mpeg2dec->buffers[i] == buf) {
      GST_DEBUG_OBJECT (mpeg2dec, "Releasing %p at slot %d", buf, i);
      gst_buffer_unref (buf);
      mpeg2dec->buffers[i] = NULL;

      return TRUE;
    }
  }
  return FALSE;
}

static void
free_all_buffers (GstMpeg2dec * mpeg2dec)
{
  int i;

  for (i = 0; i < GST_MPEG2DEC_NUM_BUFS; i++) {
    if (mpeg2dec->buffers[i] != NULL) {
      GST_DEBUG_OBJECT (mpeg2dec, "free_all Releasing %p at slot %d",
          mpeg2dec->buffers[i], i);
      gst_buffer_unref (mpeg2dec->buffers[i]);
      mpeg2dec->buffers[i] = NULL;
    }
  }
}

static GstBuffer *
crop_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * input)
{
  unsigned char *in_data;
  unsigned char *out_data;
  unsigned int h_subsample;
  unsigned int v_subsample;
  unsigned int line;
  GstBuffer *outbuf = input;

  /*We crop only if the target region is smaller than the input one */
  if ((mpeg2dec->decoded_width > mpeg2dec->width) ||
      (mpeg2dec->decoded_height > mpeg2dec->height)) {
    /* If we don't know about the format, we just return the original
     * buffer.
     */
    if (mpeg2dec->format == MPEG2DEC_FORMAT_I422 ||
        mpeg2dec->format == MPEG2DEC_FORMAT_I420 ||
        mpeg2dec->format == MPEG2DEC_FORMAT_YV12) {
      /*FIXME:  I have tried to use gst_buffer_copy_on_write, but it
       *        still have some artifact, so I'me allocating new buffer
       *        for each frame decoded...
       */
      if (mpeg2dec->format == MPEG2DEC_FORMAT_I422) {
        outbuf =
            gst_buffer_new_and_alloc (mpeg2dec->width * mpeg2dec->height * 2);
        h_subsample = 2;
        v_subsample = 1;
      } else {
        outbuf =
            gst_buffer_new_and_alloc (mpeg2dec->width * mpeg2dec->height * 1.5);
        h_subsample = 2;
        v_subsample = 2;
      }

      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (input);
      GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (input);
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (input);

      /* Copy Y first */
      in_data = GST_BUFFER_DATA (input);
      out_data = GST_BUFFER_DATA (outbuf);
      for (line = 0; line < mpeg2dec->height; line++) {
        memcpy (out_data, in_data, mpeg2dec->width);
        out_data += mpeg2dec->width;
        in_data += mpeg2dec->decoded_width;
      }

      /* Now copy U & V */
      in_data =
          GST_BUFFER_DATA (input) +
          mpeg2dec->decoded_width * mpeg2dec->decoded_height;
      for (line = 0; line < mpeg2dec->height / v_subsample; line++) {
        memcpy (out_data, in_data, mpeg2dec->width / h_subsample);
        memcpy (out_data +
            mpeg2dec->width * mpeg2dec->height / (v_subsample * h_subsample),
            in_data +
            mpeg2dec->decoded_width * mpeg2dec->decoded_height / (v_subsample *
                h_subsample), mpeg2dec->width / h_subsample);
        out_data += mpeg2dec->width / h_subsample;
        in_data += mpeg2dec->decoded_width / h_subsample;
      }

      gst_buffer_unref (input);
    }
  }

  return outbuf;
}

static GstBuffer *
gst_mpeg2dec_alloc_buffer (GstMpeg2dec * mpeg2dec, gint64 offset)
{
  GstBuffer *outbuf = NULL;
  gint size = mpeg2dec->decoded_width * mpeg2dec->decoded_height;
  guint8 *buf[3], *out = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  do {

    if (mpeg2dec->format == MPEG2DEC_FORMAT_I422) {
      ret =
          gst_pad_alloc_buffer (mpeg2dec->srcpad, GST_BUFFER_OFFSET_NONE,
          size * 2, GST_PAD_CAPS (mpeg2dec->srcpad), &outbuf);
      if (ret != GST_FLOW_OK) {
        GST_ELEMENT_ERROR (mpeg2dec, RESOURCE, FAILED, (NULL),
            ("Failed to allocate memory for buffer"));
        break;
      }
      /* 0.9
         outbuf =
         gst_pad_alloc_buffer (mpeg2dec->srcpad, GST_BUFFER_OFFSET_NONE,
         size * 2);

         if (!outbuf) {
         GST_ELEMENT_ERROR (mpeg2dec, RESOURCE, FAILED, (NULL),
         ("Failed to allocate memory for buffer"));
         return NULL;
         }
       */

      out = GST_BUFFER_DATA (outbuf);

      buf[0] = out;
      buf[1] = buf[0] + size;
      buf[2] = buf[1] + size / 2;

    } else {
      /* 0.9
         outbuf =
         gst_pad_alloc_buffer (mpeg2dec->srcpad, GST_BUFFER_OFFSET_NONE,
         (size * 3) / 2);
       */
      ret =
          gst_pad_alloc_buffer (mpeg2dec->srcpad, GST_BUFFER_OFFSET_NONE,
          size * 2, GST_PAD_CAPS (mpeg2dec->srcpad), &outbuf);
      if (ret != GST_FLOW_OK) {
        GST_ELEMENT_ERROR (mpeg2dec, RESOURCE, FAILED, (NULL),
            ("Failed to allocate memory for buffer"));
        break;
      }

      out = GST_BUFFER_DATA (outbuf);

      buf[0] = out;
      if (mpeg2dec->format == MPEG2DEC_FORMAT_I420) {
        buf[0] = out;
        buf[1] = buf[0] + size;
        buf[2] = buf[1] + size / 4;
      } else {
        buf[0] = out;
        buf[2] = buf[0] + size;
        buf[1] = buf[2] + size / 4;
      }
    }

    if (!put_buffer (mpeg2dec, outbuf)) {
#if 0
      GST_ELEMENT_ERROR (mpeg2dec, LIBRARY, TOO_LAZY, (NULL),
          ("No free slot. libmpeg2 did not discard buffers."));
#else
      GST_WARNING_OBJECT (mpeg2dec,
          "No free slot. libmpeg2 did not discard buffers.");
#endif
      return NULL;
    }

    mpeg2_custom_fbuf (mpeg2dec->decoder, 1);
    mpeg2_set_buf (mpeg2dec->decoder, buf, outbuf);

    /* we store the original byteoffset of this picture in the stream here
     * because we need it for indexing */
    GST_BUFFER_OFFSET (outbuf) = offset;

  }
  while (FALSE);

  if (ret != GST_FLOW_OK) {
    outbuf = NULL;              /* just to asure NULL return, looking the path
                                   above it happens only when gst_pad_alloc_buffer
                                   fails to alloc outbf */
  }

  return outbuf;
}

static gboolean
gst_mpeg2dec_negotiate_format (GstMpeg2dec * mpeg2dec)
{
  GstCaps *caps;
  guint32 fourcc, myFourcc;
  gboolean ret = TRUE;
  const mpeg2_info_t *info;
  const mpeg2_sequence_t *sequence;

  if (!GST_PAD_IS_LINKED (mpeg2dec->srcpad)) {
    mpeg2dec->format = MPEG2DEC_FORMAT_I420;
    return TRUE;
  }

  info = mpeg2_info (mpeg2dec->decoder);
  sequence = info->sequence;

  if (sequence->width != sequence->chroma_width &&
      sequence->height != sequence->chroma_height)
    myFourcc = GST_STR_FOURCC ("I420");
  else if (sequence->width == sequence->chroma_width ||
      sequence->height == sequence->chroma_height)
    myFourcc = GST_STR_FOURCC ("Y42B");
  else {
    g_warning ("mpeg2dec: 4:4:4 format not yet supported");
    return (FALSE);
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, myFourcc,
      "width", G_TYPE_INT, mpeg2dec->width,
      "height", G_TYPE_INT, mpeg2dec->height,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, mpeg2dec->pixel_width,
      mpeg2dec->pixel_height,
      "framerate", G_TYPE_DOUBLE, mpeg2dec->frame_rate, NULL);

  // ret = gst_pad_set_explicit_caps (mpeg2dec->srcpad, caps);
  ret = gst_pad_set_caps (mpeg2dec->srcpad, caps);
  if (!ret)
    return FALSE;

  /* it worked, try to find what it was again */
  gst_structure_get_fourcc (gst_caps_get_structure (caps, 0),
      "format", &fourcc);

  if (fourcc == GST_STR_FOURCC ("Y42B")) {
    mpeg2dec->format = MPEG2DEC_FORMAT_I422;
  } else if (fourcc == GST_STR_FOURCC ("I420")) {
    mpeg2dec->format = MPEG2DEC_FORMAT_I420;
  } else {
    mpeg2dec->format = MPEG2DEC_FORMAT_YV12;
  }

  return TRUE;
}

static gboolean
handle_sequence (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  gint i;

  mpeg2dec->width = info->sequence->picture_width;
  mpeg2dec->height = info->sequence->picture_height;
  mpeg2dec->pixel_width = info->sequence->pixel_width;
  mpeg2dec->pixel_height = info->sequence->pixel_height;
  mpeg2dec->decoded_width = info->sequence->width;
  mpeg2dec->decoded_height = info->sequence->height;
  mpeg2dec->total_frames = 0;

  /* find framerate */
  for (i = 0; i < 9; i++) {
    if (info->sequence->frame_period == frame_periods[i]) {
      mpeg2dec->frame_rate = fpss[i];
    }
  }
  mpeg2dec->frame_period = info->sequence->frame_period * GST_USECOND / 27;

  GST_DEBUG_OBJECT (mpeg2dec,
      "sequence flags: %d, frame period: %d (%g), frame rate: %g",
      info->sequence->flags, info->sequence->frame_period,
      (double) (mpeg2dec->frame_period) / GST_SECOND, mpeg2dec->frame_rate);
  GST_DEBUG_OBJECT (mpeg2dec, "profile: %02x, colour_primaries: %d",
      info->sequence->profile_level_id, info->sequence->colour_primaries);
  GST_DEBUG_OBJECT (mpeg2dec, "transfer chars: %d, matrix coef: %d",
      info->sequence->transfer_characteristics,
      info->sequence->matrix_coefficients);

  if (!gst_mpeg2dec_negotiate_format (mpeg2dec)) {
    GST_ELEMENT_ERROR (mpeg2dec, CORE, NEGOTIATION, (NULL), (NULL));
    return FALSE;
  }

  free_all_buffers (mpeg2dec);
  if (!gst_mpeg2dec_alloc_buffer (mpeg2dec, mpeg2dec->offset) ||
      !gst_mpeg2dec_alloc_buffer (mpeg2dec, mpeg2dec->offset) ||
      !gst_mpeg2dec_alloc_buffer (mpeg2dec, mpeg2dec->offset))
    return FALSE;

  mpeg2dec->need_sequence = FALSE;

  return TRUE;
}

static gboolean
handle_picture (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  gboolean key_frame = FALSE;
  GstBuffer *outbuf;

  if (info->current_picture) {
    key_frame =
        (info->current_picture->flags & PIC_MASK_CODING_TYPE) ==
        PIC_FLAG_CODING_TYPE_I;
  }
  outbuf = gst_mpeg2dec_alloc_buffer (mpeg2dec, mpeg2dec->offset);
  if (!outbuf)
    return FALSE;

  GST_DEBUG_OBJECT (mpeg2dec, "picture %s, outbuf %p, offset %"
      G_GINT64_FORMAT,
      key_frame ? ", kf," : "    ", outbuf, GST_BUFFER_OFFSET (outbuf)
      );

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_PICTURE && key_frame)
    mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_KEYFRAME;

  if (!GST_PAD_IS_USABLE (mpeg2dec->srcpad))
    mpeg2_skip (mpeg2dec->decoder, 1);
  else
    mpeg2_skip (mpeg2dec->decoder, 0);

  return TRUE;
}

static gboolean
handle_slice (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstBuffer *outbuf = NULL;
  gboolean skip = FALSE;

  GST_DEBUG_OBJECT (mpeg2dec, "picture slice/end %p %p %p %p",
      info->display_fbuf,
      info->display_picture, info->current_picture,
      (info->display_fbuf ? info->display_fbuf->id : NULL));

  if (info->display_fbuf && info->display_fbuf->id) {
    const mpeg2_picture_t *picture;
    gboolean key_frame = FALSE;

    outbuf = GST_BUFFER (info->display_fbuf->id);
    if (!check_buffer (mpeg2dec, outbuf)) {
      GST_ELEMENT_ERROR (mpeg2dec, RESOURCE, FAILED, (NULL),
          ("libmpeg2 reported invalid buffer %p", outbuf));
      return FALSE;
    }

    gst_buffer_ref (outbuf);

    picture = info->display_picture;

    key_frame =
        (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I;
    GST_DEBUG_OBJECT (mpeg2dec, "picture keyframe %d", key_frame);

    if (key_frame) {
      /* not need for decode GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_DELTA_UNIT); */
      /* 0.9 GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT); */
    }
    if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_KEYFRAME && key_frame)
      mpeg2dec->discont_state = MPEG2DEC_DISC_NONE;

#if MPEG2_RELEASE < MPEG2_VERSION(0,4,0)
    if (picture->flags & PIC_FLAG_PTS) {
      GstClockTime time = MPEG_TIME_TO_GST_TIME (picture->pts);
#else
    if (picture->flags & PIC_FLAG_TAGS) {
      GstClockTime time =
          MPEG_TIME_TO_GST_TIME ((GstClockTime) (picture->
              tag2) << 32 | picture->tag);
#endif
      GST_DEBUG_OBJECT (mpeg2dec,
          "picture had pts %" GST_TIME_FORMAT ", we had %"
          GST_TIME_FORMAT, GST_TIME_ARGS (time),
          GST_TIME_ARGS (mpeg2dec->next_time));
      GST_BUFFER_TIMESTAMP (outbuf) = mpeg2dec->next_time = time;
    } else {
      GST_DEBUG_OBJECT (mpeg2dec,
          "picture didn't have pts. Using %" GST_TIME_FORMAT,
          GST_TIME_ARGS (mpeg2dec->next_time));
      GST_BUFFER_TIMESTAMP (outbuf) = mpeg2dec->next_time;
    }

    /* TODO set correct offset here based on frame number */
    if (info->display_picture_2nd) {
      GST_BUFFER_DURATION (outbuf) = (picture->nb_fields +
          info->display_picture_2nd->nb_fields) * mpeg2dec->frame_period / 2;
    } else {
      GST_BUFFER_DURATION (outbuf) =
          picture->nb_fields * mpeg2dec->frame_period / 2;
    }
    mpeg2dec->next_time += GST_BUFFER_DURATION (outbuf);

    GST_DEBUG_OBJECT (mpeg2dec,
        "picture: %s %s fields:%d off:%" G_GINT64_FORMAT " ts:%"
        GST_TIME_FORMAT,
        (picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? "tff " : "    "),
        (picture->flags & PIC_FLAG_PROGRESSIVE_FRAME ? "prog" : "    "),
        picture->nb_fields, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

    if (mpeg2dec->index) {
      gst_index_add_association (mpeg2dec->index, mpeg2dec->index_id,
          (key_frame ? GST_ASSOCIATION_FLAG_KEY_UNIT : 0),
          GST_FORMAT_BYTES, GST_BUFFER_OFFSET (outbuf),
          GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (outbuf), 0);
    }

    if (picture->flags & PIC_FLAG_SKIP) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer because of skip flag");
      gst_buffer_unref (outbuf);
    } else if (!GST_PAD_IS_USABLE (mpeg2dec->srcpad)
        || gst_pad_get_negotiated_caps (mpeg2dec->srcpad) == NULL) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, pad not usable");
      gst_buffer_unref (outbuf);
    } else if (mpeg2dec->discont_state != MPEG2DEC_DISC_NONE) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, discont state %d",
          mpeg2dec->discont_state);
      gst_buffer_unref (outbuf);
    } else if (mpeg2dec->next_time < mpeg2dec->segment_start) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, next_time %"
          GST_TIME_FORMAT " <  segment_start %" GST_TIME_FORMAT,
          GST_TIME_ARGS (mpeg2dec->next_time),
          GST_TIME_ARGS (mpeg2dec->segment_start));
      gst_buffer_unref (outbuf);
    } else if (skip) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, asked to skip");
      gst_buffer_unref (outbuf);
    } else {
      GST_LOG_OBJECT (mpeg2dec, "pushing buffer, timestamp %"
          GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

      if ((mpeg2dec->decoded_height > mpeg2dec->height) ||
          (mpeg2dec->decoded_width > mpeg2dec->width))
        outbuf = crop_buffer (mpeg2dec, outbuf);

      if (info->current_picture
          && (info->current_picture->flags & PIC_MASK_CODING_TYPE) ==
          PIC_FLAG_CODING_TYPE_I) {
        /* not need for decode GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DELTA_UNIT); */
        /* 0.9 GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT); */
      } else {
        /* not need for decode GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DELTA_UNIT); */
        /* 0.9 GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_KEY_UNIT); */
      }
      gst_pad_push (mpeg2dec->srcpad, GST_BUFFER (outbuf));
    }
  } else if (info->display_fbuf && !info->display_fbuf->id) {
    GST_ELEMENT_ERROR (mpeg2dec, LIBRARY, TOO_LAZY, (NULL),
        ("libmpeg2 reported invalid buffer %p", info->discard_fbuf->id));
  }

  if (info->discard_fbuf && info->discard_fbuf->id) {
    if (free_buffer (mpeg2dec, GST_BUFFER (info->discard_fbuf->id))) {
      GST_DEBUG_OBJECT (mpeg2dec, "Discarded buffer %p",
          info->discard_fbuf->id);
    } else {
      GST_ELEMENT_ERROR (mpeg2dec, LIBRARY, TOO_LAZY, (NULL),
          ("libmpeg2 reported invalid buffer %p", info->discard_fbuf->id));
    }
  }
  return TRUE;
}

#if 0
static void
update_streaminfo (GstMpeg2dec * mpeg2dec)
{
  GstCaps *caps;
  GstProps *props;
  GstPropsEntry *entry;
  const mpeg2_info_t *info;

  info = mpeg2_info (mpeg2dec->decoder);

  props = gst_props_empty_new ();

  entry =
      gst_props_entry_new ("framerate",
      G_TYPE_DOUBLE (GST_SECOND / (float) mpeg2dec->frame_period));
  gst_props_add_entry (props, entry);
  entry =
      gst_props_entry_new ("bitrate",
      G_TYPE_INT (info->sequence->byte_rate * 8));
  gst_props_add_entry (props, entry);

  caps = gst_caps_new ("mpeg2dec_streaminfo",
      "application/x-gst-streaminfo", props);

  gst_caps_replace_sink (&mpeg2dec->streaminfo, caps);
  g_object_notify (G_OBJECT (mpeg2dec), "streaminfo");
}
#endif

static void
gst_mpeg2dec_flush_decoder (GstMpeg2dec * mpeg2dec)
{
  if (mpeg2dec->decoder) {
    const mpeg2_info_t *info = mpeg2_info (mpeg2dec->decoder);
    mpeg2_state_t state;

    /*
     * iterate the decoder and free buffers
     */
    do {
      state = mpeg2_parse (mpeg2dec->decoder);
      switch (state) {
        case STATE_SEQUENCE:
          if (!handle_sequence (mpeg2dec, info)) {
            gst_mpeg2dec_close_decoder (mpeg2dec);
            gst_mpeg2dec_open_decoder (mpeg2dec);
            state = -1;
          }
          break;
        case STATE_PICTURE:
          if (!handle_picture (mpeg2dec, info)) {
            gst_mpeg2dec_close_decoder (mpeg2dec);
            gst_mpeg2dec_open_decoder (mpeg2dec);
            state = -1;
            break;
          }
          mpeg2_skip (mpeg2dec->decoder, 1);
          break;
        case STATE_END:
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 4, 0)
        case STATE_INVALID_END:
#endif
        case STATE_SLICE:
          if (info->discard_fbuf) {
            if (free_buffer (mpeg2dec, GST_BUFFER (info->discard_fbuf->id))) {
              GST_DEBUG_OBJECT (mpeg2dec, "Discarded buffer %p",
                  info->discard_fbuf->id);
            } else {
              GST_ELEMENT_ERROR (mpeg2dec, LIBRARY, TOO_LAZY, (NULL),
                  ("libmpeg2 reported invalid buffer %p, fbuf: %p",
                      info->discard_fbuf->id, info->discard_fbuf));
            }
          }
          break;
        case STATE_INVALID:
          GST_WARNING_OBJECT (mpeg2dec, "Decoding error");
          /*
           * We need to close the decoder here, according to docs
           */
          gst_mpeg2dec_close_decoder (mpeg2dec);
          gst_mpeg2dec_open_decoder (mpeg2dec);
          return;
        default:
          break;
      }
    }
    while (state != STATE_BUFFER && state != -1);

#if MPEG2_RELEASE >= MPEG2_VERSION(0,4,0)
    GST_DEBUG_OBJECT (mpeg2dec, "resetting mpeg2 stream decoder");
    /* 0 starts at next picture, 1 at next sequence header */
    mpeg2_reset (mpeg2dec->decoder, 0);
#endif

  }
}

static GstFlowReturn
gst_mpeg2dec_chain (GstPad * pad, GstBuffer * buf)
{
  /* 0.9  GstBuffer *buf = GST_BUFFER (_data); */
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));
  guint32 size;
  guint8 *data, *end;
  GstClockTime pts;
  const mpeg2_info_t *info;
  mpeg2_state_t state;
  gboolean done = FALSE;



  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);
  pts = GST_BUFFER_TIMESTAMP (buf);
  GST_LOG_OBJECT (mpeg2dec, "received buffer, timestamp %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  info = mpeg2_info (mpeg2dec->decoder);
  end = data + size;

  if (pts != GST_CLOCK_TIME_NONE) {
    gint64 mpeg_pts = GST_TIME_TO_MPEG_TIME (pts);

    GST_DEBUG_OBJECT (mpeg2dec,
        "have pts: %" G_GINT64_FORMAT " (%" GST_TIME_FORMAT ")", mpeg_pts,
        GST_TIME_ARGS (MPEG_TIME_TO_GST_TIME (mpeg_pts)));

#if MPEG2_RELEASE >= MPEG2_VERSION(0,4,0)
    mpeg2_tag_picture (mpeg2dec->decoder, mpeg_pts & 0xffffffff,
        mpeg_pts >> 32);
#else
    mpeg2_pts (mpeg2dec->decoder, mpeg_pts);
#endif

  } else {
    GST_LOG ("no pts");
  }

  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer");
  mpeg2_buffer (mpeg2dec->decoder, data, end);
  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer done");

  mpeg2dec->offset = GST_BUFFER_OFFSET (buf);
  while (!done) {
    GST_LOG_OBJECT (mpeg2dec, "calling parse");
    state = mpeg2_parse (mpeg2dec->decoder);
    GST_DEBUG_OBJECT (mpeg2dec, "parse state %d", state);
    switch (state) {
      case STATE_SEQUENCE:
        if (!handle_sequence (mpeg2dec, info)) {
          goto exit;
        }

        if (mpeg2dec->pending_event) {
          done =
              GST_EVENT_SEEK_FLAGS (mpeg2dec->
              pending_event) & GST_SEEK_FLAG_FLUSH;

          gst_mpeg2dec_src_event (mpeg2dec->srcpad, mpeg2dec->pending_event);
          mpeg2dec->pending_event = NULL;
        }
        break;
      case STATE_SEQUENCE_REPEATED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence repeated");
      case STATE_GOP:
        break;
      case STATE_PICTURE:
      {
        if (!handle_picture (mpeg2dec, info)) {
          goto exit;
        }
        break;
      }
      case STATE_SLICE_1ST:
        GST_LOG_OBJECT (mpeg2dec, "1st slice of frame encountered");
        break;
      case STATE_PICTURE_2ND:
        GST_LOG_OBJECT (mpeg2dec,
            "Second picture header encountered. Decoding 2nd field");
        break;
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 4, 0)
      case STATE_INVALID_END:
#endif
      case STATE_END:
        mpeg2dec->need_sequence = TRUE;
      case STATE_SLICE:
        if (!handle_slice (mpeg2dec, info)) {
          goto exit;
        }

        break;
      case STATE_BUFFER:
      case -1:
        /* need more data */
        done = TRUE;
        break;
        /* error */
      case STATE_INVALID:
        GST_WARNING_OBJECT (mpeg2dec, "Decoding error");
        goto exit;

        break;
      default:
        GST_ERROR_OBJECT (mpeg2dec, "Unknown libmpeg2 state %d, FIXME", state);

        break;
    }

    /*
     * FIXME: should pass more information such as state the user data is from
     */
#ifdef enable_user_data
    if (info->user_data_len > 0) {
      if (GST_PAD_IS_USABLE (mpeg2dec->userdatapad)) {
        GstBuffer *udbuf = gst_buffer_new_and_alloc (info->user_data_len);

        memcpy (GST_BUFFER_DATA (udbuf), info->user_data, info->user_data_len);

        gst_pad_push (mpeg2dec->userdatapad, GST_BUFFER (udbuf));
      }
    }
#endif

  }
  gst_buffer_unref (buf);
  return GST_FLOW_OK;

exit:
  /*
   * Close and reopen the decoder, because
   * something went pretty wrong
   */
  gst_mpeg2dec_close_decoder (mpeg2dec);
  gst_mpeg2dec_open_decoder (mpeg2dec);
  gst_buffer_unref (buf);
  return GST_FLOW_ERROR;
}

static gboolean
gst_mpeg2dec_sink_event (GstPad * pad, GstEvent * event)
{

  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 time;
      gint64 end_time;          /* 0.9 just to call
                                   gst_event_discont_get_value, non
                                   used anywhere else */

      GST_STREAM_LOCK (pad);

      if (!gst_event_discont_get_value (event, GST_FORMAT_TIME, &time,
              &end_time)
          || !GST_CLOCK_TIME_IS_VALID (time)) {
        GST_WARNING_OBJECT (mpeg2dec,
            "No new time offset in discont event %p", event);
      } else {
        mpeg2dec->next_time = time;
        GST_DEBUG_OBJECT (mpeg2dec,
            "discont, reset next_time to %" G_GUINT64_FORMAT " (%"
            GST_TIME_FORMAT ")", mpeg2dec->next_time,
            GST_TIME_ARGS (mpeg2dec->next_time));
      }

      // what's hell is that
      /*
         if (GST_EVENT_DISCONT_NEW_MEDIA (event))
         {
         gst_mpeg2dec_reset (mpeg2dec);
         }
         else
         {
         mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
         gst_mpeg2dec_flush_decoder (mpeg2dec);
         }
       */

      GST_STREAM_UNLOCK (pad);
      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH:
    {
      mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
      gst_mpeg2dec_flush_decoder (mpeg2dec);
      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_EOS:
      GST_STREAM_LOCK (pad);
      if (mpeg2dec->index && mpeg2dec->closed) {
        gst_index_commit (mpeg2dec->index, mpeg2dec->index_id);
      }
      GST_STREAM_UNLOCK (pad);
      ret = gst_pad_event_default (pad, event);
      break;

    default:
      GST_DEBUG_OBJECT (mpeg2dec, "Got event of type %d on sink pad",
          GST_EVENT_TYPE (event));
      ret = gst_pad_event_default (pad, event);
      break;
  }

  return ret;

}



#if 0
static const GstFormat *
gst_mpeg2dec_get_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    0
  };
  static GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}
#endif


static GstCaps *
gst_mpeg2dec_src_getcaps (GstPad * pad)
{
  GstCaps *caps;

  GST_LOCK (pad);
  if (!(caps = GST_PAD_CAPS (pad)))
    caps = (GstCaps *) gst_pad_get_pad_template_caps (pad);
  caps = gst_caps_ref (caps);
  GST_UNLOCK (pad);

  return caps;
}

static gboolean
gst_mpeg2dec_sink_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  const mpeg2_info_t *info;

  mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));

  if (mpeg2dec->decoder == NULL)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  info = mpeg2_info (mpeg2dec->decoder);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          if (info->sequence && info->sequence->byte_rate) {
            *dest_value = GST_SECOND * src_value / info->sequence->byte_rate;
            break;
          }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          if (info->sequence && info->sequence->byte_rate) {
            *dest_value = src_value * info->sequence->byte_rate / GST_SECOND;
            break;
          }
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}


static gboolean
gst_mpeg2dec_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  const mpeg2_info_t *info;
  guint64 scale = 1;

  mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));

  if (mpeg2dec->decoder == NULL)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  info = mpeg2_info (mpeg2dec->decoder);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = 6 * (mpeg2dec->width * mpeg2dec->height >> 2);
        case GST_FORMAT_DEFAULT:
          if (info->sequence && mpeg2dec->frame_period) {
            *dest_value = src_value * scale / mpeg2dec->frame_period;
            break;
          }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * mpeg2dec->frame_period;
          break;
        case GST_FORMAT_BYTES:
          *dest_value =
              src_value * 6 * ((mpeg2dec->width * mpeg2dec->height) >> 2);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static const GstQueryType *
gst_mpeg2dec_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_mpeg2dec_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      GstFormat rformat;
      gint64 cur, total, total_bytes;
      GstPad *peer;

      /* save requested format */
      gst_query_parse_position (query, &format, NULL, NULL);

      /* query peer for total length in bytes */
      gst_query_set_position (query, GST_FORMAT_BYTES, -1, -1);

      if ((peer = gst_pad_get_peer (mpeg2dec->sinkpad)) == NULL)
        goto error;

      if (!gst_pad_query (peer, query)) {
        GST_LOG_OBJECT (mpeg2dec, "query on peer pad failed");
        goto error;
      }
      gst_object_unref (peer);

      /* get the returned format */
      gst_query_parse_position (query, &rformat, NULL, &total_bytes);
      if (rformat == GST_FORMAT_BYTES)
        GST_LOG_OBJECT (mpeg2dec, "peer pad returned total=%lld bytes",
            total_bytes);
      else if (rformat == GST_FORMAT_TIME)
        GST_LOG_OBJECT (mpeg2dec, "peer pad returned time=%lld", total_bytes);

      /* Check if requested format is returned format */
      if (format == rformat)
        return TRUE;

      /* and convert to the requested format */
      if (format != GST_FORMAT_DEFAULT) {
        if (!gst_mpeg2dec_src_convert (pad, GST_FORMAT_DEFAULT,
                mpeg2dec->next_time, &format, &cur))
          goto error;
      } else {
        cur = mpeg2dec->next_time;
      }

      if (total_bytes != -1) {
        if (format != GST_FORMAT_BYTES) {
          if (!gst_mpeg2dec_sink_convert (pad, GST_FORMAT_BYTES, total_bytes,
                  &format, &total))
            goto error;
        } else {
          total = total_bytes;
        }
      } else {
        total = -1;
      }

      gst_query_set_position (query, format, cur, total);

      GST_LOG_OBJECT (mpeg2dec,
          "position query: peer returned total: %llu - we return %llu (format %u)",
          total, cur, format);

      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;

error:

  GST_DEBUG ("error handling query");
  return FALSE;
}


#if 0
static const GstEventMask *
gst_mpeg2dec_get_event_masks (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {GST_EVENT_NAVIGATION, GST_EVENT_FLAG_NONE},
    {0,}
  };

  return masks;
}
#endif

static gboolean
index_seek (GstPad * pad, GstEvent * event)
{
  GstIndexEntry *entry;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));

  entry = gst_index_get_assoc_entry (mpeg2dec->index, mpeg2dec->index_id,
      GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_KEY_UNIT,
      GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event));

  if ((entry) && GST_PAD_PEER (mpeg2dec->sinkpad)) {
    const GstFormat *peer_formats, *try_formats;

    /* since we know the exact byteoffset of the frame, make sure to seek on bytes first */
    const GstFormat try_all_formats[] = {
      GST_FORMAT_BYTES,
      GST_FORMAT_TIME,
      0
    };

    try_formats = try_all_formats;

#if 0
    peer_formats = gst_pad_get_formats (GST_PAD_PEER (mpeg2dec->sinkpad));
#else
    peer_formats = try_all_formats;     /* FIXE ME */
#endif

    while (gst_formats_contains (peer_formats, *try_formats)) {
      gint64 value;

      if (gst_index_entry_assoc_map (entry, *try_formats, &value)) {
        GstEvent *seek_event;

        GST_CAT_DEBUG (GST_CAT_SEEK, "index %s %" G_GINT64_FORMAT
            " -> %s %" G_GINT64_FORMAT,
            gst_format_get_details (GST_EVENT_SEEK_FORMAT (event))->nick,
            GST_EVENT_SEEK_OFFSET (event),
            gst_format_get_details (*try_formats)->nick, value);

        /* lookup succeeded, create the seek */
        seek_event =
            gst_event_new_seek (*try_formats | GST_SEEK_METHOD_SET |
            GST_SEEK_FLAG_FLUSH, value);
        /* do the seek */
        if (gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), seek_event)) {
          /* seek worked, we're done, loop will exit */
#if 0
          mpeg2dec->segment_start = GST_EVENT_SEEK_OFFSET (event);
#endif
          return TRUE;
        }
      }
      try_formats++;
    }
  }
  return FALSE;
}


static gboolean
normal_seek (GstPad * pad, GstEvent * event)
{
  gint64 time_offset, bytes_offset;
  GstFormat format;
  guint flush;

  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));

  /* const GstFormat *peer_formats; */
  gboolean res;

  GST_DEBUG ("normal seek");

  format = GST_FORMAT_TIME;
  if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_TIME) {
    if (!gst_mpeg2dec_src_convert (pad, GST_EVENT_SEEK_FORMAT (event),
            GST_EVENT_SEEK_OFFSET (event), &format, &time_offset)) {
      /* probably unsupported seek format */
      GST_DEBUG ("failed to convert format %u into GST_FORMAT_TIME",
          GST_EVENT_SEEK_FORMAT (event));
      return FALSE;
    }
  } else {
    time_offset = GST_EVENT_SEEK_OFFSET (event);
  }

  GST_DEBUG ("seek to time %" GST_TIME_FORMAT, GST_TIME_ARGS (time_offset));

  /* shave off the flush flag, we'll need it later */
  flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

  /* assume the worst */
  res = FALSE;

  format = GST_FORMAT_BYTES;
  if (gst_mpeg2dec_sink_convert (pad, GST_FORMAT_TIME, time_offset,
          &format, &bytes_offset)) {
    GstEvent *seek_event;

    /* conversion succeeded, create the seek */
    seek_event =
        gst_event_new_seek (format | GST_EVENT_SEEK_METHOD (event) | flush,
        bytes_offset);

    /* do the seek */
    res = gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), seek_event);
  }
#if 0

  /* get our peer formats */
  if (GST_PAD_PEER (mpeg2dec->sinkpad))
    peer_formats = gst_pad_get_formats (GST_PAD_PEER (mpeg2dec->sinkpad));

  /* while we did not exhaust our seek formats without result */
  while (peer_formats && *peer_formats) {
    gint64 desired_offset;

    format = *peer_formats;

    /* try to convert requested format to one we can seek with on the sinkpad */
    if (gst_mpeg2dec_sink_convert (mpeg2dec->sinkpad, GST_FORMAT_TIME,
            src_offset, &format, &desired_offset)) {
      GstEvent *seek_event;

      /* conversion succeeded, create the seek */
      seek_event =
          gst_event_new_seek (format | GST_SEEK_METHOD_SET | flush,
          desired_offset);
      /* do the seekk */
      if (gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), seek_event)) {
        /* seek worked, we're done, loop will exit */
        res = TRUE;
        break;
      }
    }
    peer_formats++;
  }
  /* at this point, either the seek worked and res = TRUE or res == FALSE and the seek
   * failed */
  if (res && flush) {
    /* if we need to flush, iterate until the buffer is empty */
    gst_mpeg2dec_flush_decoder (mpeg2dec);
  }
#endif

  return res;
}


static gboolean
gst_mpeg2dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (GST_PAD_PARENT (pad));

  if (mpeg2dec->decoder == NULL)
    return FALSE;

  switch (GST_EVENT_TYPE (event)) {
      /* the all-formats seek logic */
    case GST_EVENT_SEEK:
      if (mpeg2dec->need_sequence) {
        mpeg2dec->pending_event = event;
        return TRUE;
      } else {
        if (mpeg2dec->index)
          res = index_seek (pad, event);
        else
          res = normal_seek (pad, event);

        if (res)
          mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
      }
      break;
    case GST_EVENT_NAVIGATION:
      /* Forward a navigation event unchanged */
      if (GST_PAD_PEER (mpeg2dec->sinkpad))
        return gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), event);

      res = FALSE;
      break;
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  return res;
}

static GstElementStateReturn
gst_mpeg2dec_change_state (GstElement * element)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
    {
      mpeg2dec->next_time = 0;

      gst_mpeg2dec_reset (mpeg2dec);
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_mpeg2dec_close_decoder (mpeg2dec);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static void
gst_mpeg2dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeg2dec *src;

  g_return_if_fail (GST_IS_MPEG2DEC (object));
  src = GST_MPEG2DEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mpeg2dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMpeg2dec *mpeg2dec;

  g_return_if_fail (GST_IS_MPEG2DEC (object));
  mpeg2dec = GST_MPEG2DEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "mpeg2dec", GST_RANK_SECONDARY,
          GST_TYPE_MPEG2DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg2dec",
    "LibMpeg2 decoder", plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN);
