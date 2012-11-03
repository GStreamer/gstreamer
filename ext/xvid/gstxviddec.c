/* GStreamer xvid decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *           (C) 2006 Mark Nauwelaerts <manauw@skynet.be>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <xvid.h>

#include <gst/video/video.h>
#include "gstxviddec.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-xvid, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ]; "
        "video/mpeg, "
        "mpegversion = (int) 4, "
        "systemstream = (boolean) FALSE, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, YV12, YVYU, UYVY }")
        "; " RGB_24_32_STATIC_CAPS (32, 0x00ff0000, 0x0000ff00,
            0x000000ff) "; " RGB_24_32_STATIC_CAPS (32, 0xff000000, 0x00ff0000,
            0x0000ff00) "; " RGB_24_32_STATIC_CAPS (32, 0x0000ff00, 0x00ff0000,
            0xff000000) "; " RGB_24_32_STATIC_CAPS (32, 0x000000ff, 0x0000ff00,
            0x00ff0000) "; " RGB_24_32_STATIC_CAPS (24, 0x0000ff, 0x00ff00,
            0xff0000) "; " GST_VIDEO_CAPS_RGB_15 "; " GST_VIDEO_CAPS_RGB_16)
    );

GST_DEBUG_CATEGORY_STATIC (xviddec_debug);
#define GST_CAT_DEFAULT xviddec_debug

static void gst_xviddec_base_init (GstXvidDecClass * klass);
static void gst_xviddec_class_init (GstXvidDecClass * klass);
static void gst_xviddec_init (GstXvidDec * dec);
static void gst_xviddec_reset (GstXvidDec * dec);
static gboolean gst_xviddec_handle_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_xviddec_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_xviddec_setcaps (GstPad * pad, GstCaps * caps);
static void gst_xviddec_flush_buffers (GstXvidDec * dec, gboolean send);
static GstStateChangeReturn gst_xviddec_change_state (GstElement * element,
    GstStateChange transition);


static GstElementClass *parent_class = NULL;

GType
gst_xviddec_get_type (void)
{
  static GType xviddec_type = 0;

  if (!xviddec_type) {
    static const GTypeInfo xviddec_info = {
      sizeof (GstXvidDecClass),
      (GBaseInitFunc) gst_xviddec_base_init,
      NULL,
      (GClassInitFunc) gst_xviddec_class_init,
      NULL,
      NULL,
      sizeof (GstXvidDec),
      0,
      (GInstanceInitFunc) gst_xviddec_init,
    };

    xviddec_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstXvidDec", &xviddec_info, 0);
  }
  return xviddec_type;
}

static void
gst_xviddec_base_init (GstXvidDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class, "XviD video decoder",
      "Codec/Decoder/Video",
      "XviD decoder based on xvidcore",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
}

static void
gst_xviddec_class_init (GstXvidDecClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (xviddec_debug, "xviddec", 0, "XviD decoder");

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_xviddec_change_state);
}


static void
gst_xviddec_init (GstXvidDec * dec)
{
  /* create the sink pad */
  dec->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xviddec_chain));
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xviddec_setcaps));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xviddec_handle_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  /* create the src pad */
  dec->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  gst_xviddec_reset (dec);
}


static void
gst_xviddec_reset (GstXvidDec * dec)
{
  /* size, etc. */
  dec->width = dec->height = dec->csp = -1;
  dec->fps_n = dec->par_n = -1;
  dec->fps_d = dec->par_d = 1;
  dec->next_ts = dec->next_dur = GST_CLOCK_TIME_NONE;
  dec->outbuf_size = 0;

  /* set xvid handle to NULL */
  dec->handle = NULL;

  /* no delayed timestamp to start with */
  dec->have_ts = FALSE;

  /* need keyframe to get going */
  dec->waiting_for_key = TRUE;
}


static void
gst_xviddec_unset (GstXvidDec * dec)
{
  /* release XviD decoder */
  xvid_decore (dec->handle, XVID_DEC_DESTROY, NULL, NULL);
  dec->handle = NULL;
}


static gboolean
gst_xviddec_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstXvidDec *dec = GST_XVIDDEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_xviddec_flush_buffers (dec, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_xviddec_flush_buffers (dec, FALSE);
      break;
    case GST_EVENT_NEWSEGMENT:
      /* don't really mind about the actual segment info,
       * but we do need to recover from this possible jump */
      /* FIXME, NEWSEGMENT is not a discontinuity. A decoder
       * should clip the output to the segment boundaries.
       * Also the rate field of the segment can be used to
       * optimize the decoding, like skipping B frames when
       * playing at double speed.
       * The DISCONT flag on buffers should be used to detect
       * discontinuities. 
       */
      dec->waiting_for_key = TRUE;
      break;
    default:
      break;
  }

  return gst_pad_push_event (dec->srcpad, event);
}


static gboolean
gst_xviddec_setup (GstXvidDec * dec)
{
  xvid_dec_create_t xdec;
  gint ret;

  /* initialise parameters, see xvid documentation */
  gst_xvid_init_struct (xdec);
  /* let the decoder handle this, don't trust the container */
  xdec.width = 0;
  xdec.height = 0;
  xdec.handle = NULL;

  GST_DEBUG_OBJECT (dec, "Initializing xvid decoder with parameters "
      "%dx%d@%d", dec->width, dec->height, dec->csp);

  if ((ret = xvid_decore (NULL, XVID_DEC_CREATE, &xdec, NULL)) < 0) {
    GST_WARNING_OBJECT (dec, "Initializing xvid decoder failed: %s (%d)",
        gst_xvid_error (ret), ret);
    return FALSE;
  }

  dec->handle = xdec.handle;

  return TRUE;
}


static void
gst_xviddec_add_par (GstStructure * structure,
    gint mux_par_n, gint mux_par_d, gint dec_par_n, gint dec_par_d)
{
  /* muxer wins if decoder has nothing interesting to offer */
  if (dec_par_n == dec_par_d) {
    gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        mux_par_n, mux_par_d, NULL);
  } else {
    gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        dec_par_n, dec_par_d, NULL);
  }
}


/* based on the decoder info, if provided, and xviddec info,
   construct a caps and send on to src pad */
static gboolean
gst_xviddec_negotiate (GstXvidDec * dec, xvid_dec_stats_t * xstats)
{
  gboolean ret;
  gint par_width, par_height;
  GstCaps *caps;

  /* note: setcaps call with no xstats info,
     so definitely need to negotiate then */
  if (xstats && (xstats->type != XVID_TYPE_VOL
          || (xstats->type == XVID_TYPE_VOL
              && dec->width == xstats->data.vol.width
              && dec->height == xstats->data.vol.height)))
    return TRUE;

  switch (xstats ? xstats->data.vol.par : XVID_PAR_11_VGA) {
    case XVID_PAR_11_VGA:
      par_width = par_height = 1;
      break;
    case XVID_PAR_43_PAL:
    case XVID_PAR_43_NTSC:
      par_width = 4;
      par_height = 3;
      break;
    case XVID_PAR_169_PAL:
    case XVID_PAR_169_NTSC:
      par_width = 16;
      par_height = 9;
      break;
    case XVID_PAR_EXT:
    default:
      par_width = xstats->data.vol.par_width;
      par_height = xstats->data.vol.par_height;
  }

  caps = gst_xvid_csp_to_caps (dec->csp, dec->width, dec->height);

  /* can only provide framerate if we received one */
  if (dec->fps_n != -1) {
    gst_structure_set (gst_caps_get_structure (caps, 0), "framerate",
        GST_TYPE_FRACTION, dec->fps_n, dec->fps_d, NULL);
  }

  gst_xviddec_add_par (gst_caps_get_structure (caps, 0),
      dec->par_n, dec->par_d, par_width, par_height);

  GST_LOG ("setting caps on source pad: %" GST_PTR_FORMAT, caps);
  ret = gst_pad_set_caps (dec->srcpad, caps);
  gst_caps_unref (caps);

  return ret;
}

static GstFlowReturn
gst_xviddec_chain (GstPad * pad, GstBuffer * buf)
{
  GstXvidDec *dec;
  GstBuffer *outbuf = NULL;
  xvid_dec_frame_t xframe;
  xvid_dec_stats_t xstats;
  gint ret;
  guint8 *data, *dupe = NULL;
  guint size;
  GstFlowReturn fret;

  dec = GST_XVIDDEC (GST_OBJECT_PARENT (pad));

  if (!dec->handle)
    goto not_negotiated;

  fret = GST_FLOW_OK;

  GST_LOG_OBJECT (dec, "Received buffer of time %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT ", size %d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_SIZE (buf));

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    /* FIXME: should we do anything here, like flush the decoder? */
  }

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* xvidcore overreads the input buffer, we need to alloc some extra padding
   * to make things work reliably */
#define EXTRA_PADDING 16
  if (EXTRA_PADDING > 0) {
    dupe = g_malloc (size + EXTRA_PADDING);
    memcpy (dupe, data, size);
    memset (dupe + size, 0, EXTRA_PADDING);
    data = dupe;
  }

  do {                          /* loop needed because xvidcore may return vol information */
    /* decode and so ... */
    gst_xvid_init_struct (xframe);
    xframe.general = XVID_LOWDELAY;
    xframe.bitstream = (void *) data;
    xframe.length = size;

    gst_xvid_init_struct (xstats);

    if (outbuf == NULL) {
      fret = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
          dec->outbuf_size, GST_PAD_CAPS (dec->srcpad), &outbuf);
      if (fret != GST_FLOW_OK)
        goto done;
    }

    gst_xvid_image_fill (&xframe.output, GST_BUFFER_DATA (outbuf),
        dec->csp, dec->width, dec->height);

    ret = xvid_decore (dec->handle, XVID_DEC_DECODE, &xframe, &xstats);
    if (ret < 0)
      goto decode_error;

    GST_LOG_OBJECT (dec, "xvid produced output, type %d, consumed %d",
        xstats.type, ret);

    if (xstats.type == XVID_TYPE_VOL)
      gst_xviddec_negotiate (dec, &xstats);

    data += ret;
    size -= ret;
  } while (xstats.type <= 0 && size > 0);

  /* 1 byte is frequently left over */
  if (size > 1) {
    GST_WARNING_OBJECT (dec, "decoder did not consume all input");
  }

  /* FIXME, reflow the multiple return exit points */
  if (xstats.type > 0) {        /* some real output was produced */
    if (G_UNLIKELY (dec->waiting_for_key)) {
      if (xstats.type != XVID_TYPE_IVOP)
        goto dropping;

      dec->waiting_for_key = FALSE;
    }
    /* bframes can cause a delay in frames being returned
       non keyframe timestamps can permute a bit between
       encode and display order, but should match for keyframes */
    if (dec->have_ts) {
      GST_BUFFER_TIMESTAMP (outbuf) = dec->next_ts;
      GST_BUFFER_DURATION (outbuf) = dec->next_dur;
      dec->next_ts = GST_BUFFER_TIMESTAMP (buf);
      dec->next_dur = GST_BUFFER_DURATION (buf);
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
    }
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (dec->srcpad));
    GST_LOG_OBJECT (dec, "pushing buffer with pts %" GST_TIME_FORMAT
        " duration %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
    fret = gst_pad_push (dec->srcpad, outbuf);

  } else {                      /* no real output yet, delay in frames being returned */
    if (G_UNLIKELY (dec->have_ts)) {
      GST_WARNING_OBJECT (dec,
          "xvid decoder produced no output, but timestamp %" GST_TIME_FORMAT
          " already queued", GST_TIME_ARGS (dec->next_ts));
    } else {
      dec->have_ts = TRUE;
      dec->next_ts = GST_BUFFER_TIMESTAMP (buf);
      dec->next_dur = GST_BUFFER_DURATION (buf);
    }
    gst_buffer_unref (outbuf);
  }

done:
  g_free (dupe);
  gst_buffer_unref (buf);

  return fret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (dec, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    fret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
decode_error:
  {
    /* FIXME: shouldn't error out fatally/properly after N decoding errors? */
    GST_ELEMENT_WARNING (dec, STREAM, DECODE, (NULL),
        ("Error decoding xvid frame: %s (%d)", gst_xvid_error (ret), ret));
    if (outbuf)
      gst_buffer_unref (outbuf);
    goto done;
  }
dropping:
  {
    GST_WARNING_OBJECT (dec, "Dropping non-keyframe (seek/init)");
    if (outbuf)
      gst_buffer_unref (outbuf);
    goto done;
  }
}


/* flush xvid encoder buffers caused by bframe usage;
   not well tested */
static void
gst_xviddec_flush_buffers (GstXvidDec * dec, gboolean send)
{
#if 0
  gint ret;
  GstBuffer *outbuf = NULL;
  xvid_dec_frame_t xframe;
  xvid_dec_stats_t xstats;
#endif

  GST_DEBUG_OBJECT (dec, "flushing buffers with send %d, have_ts %d",
      send, dec->have_ts);

  /* no need to flush if there is no delayed time-stamp */
  if (!dec->have_ts)
    return;

  /* flushing must reset the timestamp keeping */
  dec->have_ts = FALSE;

  /* also no need to flush if no handle */
  if (!dec->handle)
    return;

  /* unlike encoder, decoder does not seem to like flushing, disable for now */
#if 0
  gst_xvid_init_struct (xframe);
  gst_xvid_init_struct (xstats);

  /* init a fake frame to force flushing */
  xframe.bitstream = NULL;
  xframe.length = -1;

  ret = gst_xviddec_decode (dec, xframe, &outbuf, &xstats);
  GST_DEBUG_OBJECT (dec, "received frame when flushing, type %d, size %d",
      xstats.type, ret);

  if (ret > 0 && send) {
    /* we have some valid return frame, give it the delayed timestamp and send */
    GST_BUFFER_TIMESTAMP (outbuf) = dec->next_ts;
    GST_BUFFER_DURATION (outbuf) = dec->next_dur;

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (dec->srcpad));
    gst_pad_push (dec->srcpad, outbuf);
    return;
  }

  if (outbuf)
    gst_buffer_unref (outbuf);
#else
  return;
#endif
}

#if 0
static GstCaps *
gst_xviddec_src_getcaps (GstPad * pad)
{
  GstXvidDec *dec = GST_XVIDDEC (GST_PAD_PARENT (pad));
  GstCaps *caps;
  gint csp[] = {
    XVID_CSP_I420,
    XVID_CSP_YV12,
    XVID_CSP_YUY2,
    XVID_CSP_UYVY,
    XVID_CSP_YVYU,
    XVID_CSP_BGRA,
    XVID_CSP_ABGR,
    XVID_CSP_RGBA,
#ifdef XVID_CSP_ARGB
    XVID_CSP_ARGB,
#endif
    XVID_CSP_BGR,
    XVID_CSP_RGB555,
    XVID_CSP_RGB565,
    0
  }, i;

  if (!GST_PAD_CAPS (dec->sinkpad)) {
    GstPadTemplate *templ = gst_static_pad_template_get (&src_template);

    return gst_caps_copy (gst_pad_template_get_caps (templ));
  }

  caps = gst_caps_new_empty ();
  for (i = 0; csp[i] != 0; i++) {
    GstCaps *one = gst_xvid_csp_to_caps (csp[i], dec->width,
        dec->height, dec->fps, dec->par);

    gst_caps_append (caps, one);
  }

  return caps;
}
#endif

static gboolean
gst_xviddec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstXvidDec *dec = GST_XVIDDEC (GST_PAD_PARENT (pad));
  GstStructure *structure;
  GstCaps *allowed_caps;
  const GValue *val;

  GST_LOG_OBJECT (dec, "caps %" GST_PTR_FORMAT, caps);

  /* if there's something old around, remove it */
  if (dec->handle) {
    gst_xviddec_unset (dec);
  }

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &dec->width);
  gst_structure_get_int (structure, "height", &dec->height);

  /* perhaps some fps info */
  val = gst_structure_get_value (structure, "framerate");
  if ((val != NULL) && GST_VALUE_HOLDS_FRACTION (val)) {
    dec->fps_n = gst_value_get_fraction_numerator (val);
    dec->fps_d = gst_value_get_fraction_denominator (val);
  } else {
    dec->fps_n = -1;
    dec->fps_d = 1;
  }

  /* perhaps some par info */
  val = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (val != NULL && GST_VALUE_HOLDS_FRACTION (val)) {
    dec->par_n = gst_value_get_fraction_numerator (val);
    dec->par_d = gst_value_get_fraction_denominator (val);
  } else {
    dec->par_n = 1;
    dec->par_d = 1;
  }

  /* we try to find the preferred/accept csp */
  allowed_caps = gst_pad_get_allowed_caps (dec->srcpad);
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (dec, "... but no peer, using template caps");
    /* need to copy because get_allowed_caps returns a ref,
       and get_pad_template_caps doesn't */
    allowed_caps = gst_caps_copy (gst_pad_get_pad_template_caps (dec->srcpad));
  }
  GST_LOG_OBJECT (dec, "allowed source caps %" GST_PTR_FORMAT, allowed_caps);

  /* pick the first one ... */
  structure = gst_caps_get_structure (allowed_caps, 0);
  val = gst_structure_get_value (structure, "format");
  if (val != NULL && G_VALUE_TYPE (val) == GST_TYPE_LIST) {
    GValue temp = { 0, };
    gst_value_init_and_copy (&temp, gst_value_list_get_value (val, 0));
    gst_structure_set_value (structure, "format", &temp);
    g_value_unset (&temp);
  }

  /* ... and use its info to get the csp */
  dec->csp = gst_xvid_structure_to_csp (structure);
  if (dec->csp == -1) {
    GST_WARNING_OBJECT (dec, "failed to decide on colorspace, using I420");
    dec->csp = XVID_CSP_I420;
  }

  dec->outbuf_size =
      gst_xvid_image_get_size (dec->csp, dec->width, dec->height);

  GST_LOG_OBJECT (dec, "csp=%d, outbuf_size=%d", dec->csp, dec->outbuf_size);

  gst_caps_unref (allowed_caps);

  /* now set up xvid ... */
  if (!gst_xviddec_setup (dec)) {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), LIBRARY, INIT, (NULL), (NULL));
    return FALSE;
  }

  return gst_xviddec_negotiate (dec, NULL);
}

static GstStateChangeReturn
gst_xviddec_change_state (GstElement * element, GstStateChange transition)
{
  GstXvidDec *dec = GST_XVIDDEC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_xvid_init ())
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_xviddec_flush_buffers (dec, FALSE);
      if (dec->handle) {
        gst_xviddec_unset (dec);
      }
      gst_xviddec_reset (dec);
      break;
    default:
      break;
  }

done:
  return ret;
}
