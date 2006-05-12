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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <xvid.h>

#include <gst/video/video.h>
#include "gstxviddec.h"

/* elementfactory information */
static const GstElementDetails gst_xviddec_details =
GST_ELEMENT_DETAILS ("XviD video decoder",
    "Codec/Decoder/Video",
    "XviD decoder based on xvidcore",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-xvid, "
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


/* XvidDec signals and args */
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

GST_DEBUG_CATEGORY_STATIC (xviddec_debug);
#define GST_CAT_DEFAULT xviddec_debug

static void gst_xviddec_base_init (GstXvidDecClass * klass);
static void gst_xviddec_class_init (GstXvidDecClass * klass);
static void gst_xviddec_init (GstXvidDec * xviddec);
static void gst_xviddec_reset (GstXvidDec * xviddec);
static gboolean gst_xviddec_handle_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_xviddec_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_xviddec_setcaps (GstPad * pad, GstCaps * caps);
static void gst_xviddec_flush_buffers (GstXvidDec * xviddec, gboolean send);

#if 0
static GstPadLinkReturn
gst_xviddec_src_link (GstPad * pad, const GstCaps * vscapslist);
*/static GstCaps *gst_xviddec_src_getcaps (GstPad * pad);
#endif
static GstStateChangeReturn gst_xviddec_change_state (GstElement * element,
    GstStateChange transition);


static GstElementClass *parent_class = NULL;

/* static guint gst_xviddec_signals[LAST_SIGNAL] = { 0 }; */


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

  gst_element_class_set_details (element_class, &gst_xviddec_details);
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
gst_xviddec_init (GstXvidDec * xviddec)
{
  /* create the sink pad */
  xviddec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (xviddec), xviddec->sinkpad);

  gst_pad_set_chain_function (xviddec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xviddec_chain));
  gst_pad_set_setcaps_function (xviddec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xviddec_setcaps));
  gst_pad_set_event_function (xviddec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xviddec_handle_sink_event));

  /* create the src pad */
  xviddec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (xviddec), xviddec->srcpad);
  gst_pad_use_fixed_caps (xviddec->srcpad);

  gst_xviddec_reset (xviddec);
}


static void
gst_xviddec_reset (GstXvidDec * xviddec)
{
  /* size, etc. */
  xviddec->width = xviddec->height = xviddec->csp = -1;
  xviddec->fps_n = xviddec->par_n = -1;
  xviddec->fps_d = xviddec->par_d = 1;
  xviddec->next_ts = xviddec->next_dur = GST_CLOCK_TIME_NONE;

  /* set xvid handle to NULL */
  xviddec->handle = NULL;

  /* no delayed timestamp to start with */
  xviddec->have_ts = FALSE;

  /* need keyframe to get going */
  xviddec->waiting_for_key = TRUE;
}


static void
gst_xviddec_unset (GstXvidDec * xviddec)
{
  /* release XviD decoder */
  xvid_decore (xviddec->handle, XVID_DEC_DESTROY, NULL, NULL);
  xviddec->handle = NULL;
}


static gboolean
gst_xviddec_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstXvidDec *xviddec = GST_XVIDDEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_xviddec_flush_buffers (xviddec, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_xviddec_flush_buffers (xviddec, FALSE);
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
      xviddec->waiting_for_key = TRUE;
      break;
    default:
      break;
  }

  return gst_pad_push_event (xviddec->srcpad, event);
}


static gboolean
gst_xviddec_setup (GstXvidDec * xviddec)
{
  xvid_dec_create_t xdec;
  gint ret;

  /* initialise parameters, see xvid documentation */
  gst_xvid_init_struct (xdec);
  /* let the decoder handle this, don't trust the container */
  xdec.width = 0;
  xdec.height = 0;
  xdec.handle = NULL;

  if ((ret = xvid_decore (NULL, XVID_DEC_CREATE, &xdec, NULL)) < 0) {
    GST_DEBUG_OBJECT (xviddec,
        "Initializing xvid decoder with parameters %dx%d@%d failed: %s (%d)",
        xviddec->width, xviddec->height, xviddec->csp,
        gst_xvid_error (ret), ret);
    return FALSE;
  }

  xviddec->handle = xdec.handle;

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
gst_xviddec_negotiate (GstXvidDec * xviddec, xvid_dec_stats_t * xstats)
{
  gint par_width, par_height;
  GstCaps *caps;

  /* note: setcaps call with no xstats info,
     so definitely need to negotiate then */
  if (xstats && (xstats->type != XVID_TYPE_VOL
          || (xstats->type == XVID_TYPE_VOL
              && xviddec->width == xstats->data.vol.width
              && xviddec->height == xstats->data.vol.height)))
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

  caps = gst_xvid_csp_to_caps (xviddec->csp, xviddec->width, xviddec->height);

  /* can only provide framerate if we received one */
  if (xviddec->fps_n != -1) {
    gst_structure_set (gst_caps_get_structure (caps, 0), "framerate",
        GST_TYPE_FRACTION, xviddec->fps_n, xviddec->fps_d, NULL);
  }

  gst_xviddec_add_par (gst_caps_get_structure (caps, 0),
      xviddec->par_n, xviddec->par_d, par_width, par_height);

  return gst_pad_set_caps (xviddec->srcpad, caps);
}


/* decodes frame according to info in xframe;
   - outbuf must not be NULL
   - xstats can be NULL, if not, it has been init'ed
   - output placed in outbuf, which is also allocated if NULL,
     caller must unref when needed
   - xvid stats placed in xstats, if not NULL
   - xvid return code is returned, which is usually the size of the output frame
*/
static gint
gst_xviddec_decode (GstXvidDec * xviddec, xvid_dec_frame_t xframe,
    GstBuffer ** outbuf, xvid_dec_stats_t * xstats)
{

  g_return_val_if_fail (outbuf, -1);

  if (!*outbuf) {
    gint size = gst_xvid_image_get_size (xviddec->csp,
        xviddec->width, xviddec->height);

    gst_pad_alloc_buffer (xviddec->srcpad, GST_BUFFER_OFFSET_NONE, size,
        GST_PAD_CAPS (xviddec->srcpad), outbuf);
  }

  gst_xvid_image_fill (&xframe.output, GST_BUFFER_DATA (*outbuf),
      xviddec->csp, xviddec->width, xviddec->height);

  GST_DEBUG_OBJECT (xviddec, "decoding into buffer %" GST_PTR_FORMAT
      ", data %" GST_PTR_FORMAT ", %" GST_PTR_FORMAT, *outbuf,
      GST_BUFFER_MALLOCDATA (*outbuf), GST_BUFFER_DATA (*outbuf));

  return xvid_decore (xviddec->handle, XVID_DEC_DECODE, &xframe, xstats);
}

static GstFlowReturn
gst_xviddec_chain (GstPad * pad, GstBuffer * buf)
{
  GstXvidDec *xviddec;
  GstBuffer *outbuf = NULL;
  xvid_dec_frame_t xframe;
  xvid_dec_stats_t xstats;
  gint ret;
  guint8 *data;
  guint size;
  GstFlowReturn fret;

  xviddec = GST_XVIDDEC (GST_OBJECT_PARENT (pad));

  if (!xviddec->handle)
    goto not_negotiated;

  fret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (xviddec,
      "Received buffer of time %" GST_TIME_FORMAT ", size %d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  do {                          /* loop needed because xvidcore may return vol information */
    /* decode and so ... */
    gst_xvid_init_struct (xframe);
    xframe.general = XVID_LOWDELAY;
    xframe.bitstream = (void *) data;
    xframe.length = size;

    gst_xvid_init_struct (xstats);

    if ((ret = gst_xviddec_decode (xviddec, xframe, &outbuf, &xstats)) < 0)
      goto decode_error;

    GST_DEBUG_OBJECT (xviddec, "xvid produced output, type %d, consumed %d",
        xstats.type, ret);

    if (xstats.type == XVID_TYPE_VOL)
      gst_xviddec_negotiate (xviddec, &xstats);

    data += ret;
    size -= ret;
  } while (xstats.type <= 0 && size > 0);

  if (size > 1)                 /* 1 byte is frequently left over */
    GST_WARNING_OBJECT (xviddec,
        "xvid decoder returned frame without consuming all input");

  /* FIXME, reflow the multiple return exit points */
  if (xstats.type > 0) {        /* some real output was produced */
    if (G_UNLIKELY (xviddec->waiting_for_key)) {
      if (xstats.type != XVID_TYPE_IVOP)
        goto dropping;

      xviddec->waiting_for_key = FALSE;
    }
    /* bframes can cause a delay in frames being returned
       non keyframe timestamps can permute a bit between
       encode and display order, but should match for keyframes */
    if (xviddec->have_ts) {
      GST_BUFFER_TIMESTAMP (outbuf) = xviddec->next_ts;
      GST_BUFFER_DURATION (outbuf) = xviddec->next_dur;
      xviddec->next_ts = GST_BUFFER_TIMESTAMP (buf);
      xviddec->next_dur = GST_BUFFER_DURATION (buf);
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
    }
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (xviddec->srcpad));

    fret = gst_pad_push (xviddec->srcpad, outbuf);

  } else {                      /* no real output yet, delay in frames being returned */
    if (G_UNLIKELY (xviddec->have_ts)) {
      GST_WARNING_OBJECT (xviddec,
          "xvid decoder produced no output, but timestamp %" GST_TIME_FORMAT
          " already queued", GST_TIME_ARGS (xviddec->next_ts));
    } else {
      xviddec->have_ts = TRUE;
      xviddec->next_ts = GST_BUFFER_TIMESTAMP (buf);
      xviddec->next_dur = GST_BUFFER_TIMESTAMP (buf);
    }
    gst_buffer_unref (outbuf);
  }

done:
  gst_buffer_unref (buf);

  return fret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (xviddec, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    fret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
decode_error:
  {
    GST_ELEMENT_WARNING (xviddec, STREAM, DECODE, (NULL),
        ("Error decoding xvid frame: %s (%d)", gst_xvid_error (ret), ret));
    if (outbuf)
      gst_buffer_unref (outbuf);
    goto done;
  }
dropping:
  {
    GST_WARNING_OBJECT (xviddec, "Dropping non-keyframe (seek/init)");
    if (outbuf)
      gst_buffer_unref (outbuf);
    goto done;
  }
}


/* flush xvid encoder buffers caused by bframe usage;
   not well tested */
static void
gst_xviddec_flush_buffers (GstXvidDec * xviddec, gboolean send)
{
#if 0
  gint ret;
  GstBuffer *outbuf = NULL;
  xvid_dec_frame_t xframe;
  xvid_dec_stats_t xstats;
#endif

  GST_DEBUG_OBJECT (xviddec, "flushing buffers with send %d, have_ts %d",
      send, xviddec->have_ts);

  /* no need to flush if there is no delayed time-stamp */
  if (!xviddec->have_ts)
    return;

  /* flushing must reset the timestamp keeping */
  xviddec->have_ts = FALSE;

  /* also no need to flush if no handle */
  if (!xviddec->handle)
    return;

  /* unlike encoder, decoder does not seem to like flushing, disable for now */
#if 0
  gst_xvid_init_struct (xframe);
  gst_xvid_init_struct (xstats);

  /* init a fake frame to force flushing */
  xframe.bitstream = NULL;
  xframe.length = -1;

  ret = gst_xviddec_decode (xviddec, xframe, &outbuf, &xstats);
  GST_DEBUG_OBJECT (xviddec, "received frame when flushing, type %d, size %d",
      xstats.type, ret);

  if (ret > 0 && send) {
    /* we have some valid return frame, give it the delayed timestamp and send */
    GST_BUFFER_TIMESTAMP (outbuf) = xviddec->next_ts;
    GST_BUFFER_DURATION (outbuf) = xviddec->next_dur;

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (xviddec->srcpad));
    gst_pad_push (xviddec->srcpad, outbuf);
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
  GstXvidDec *xviddec = GST_XVIDDEC (GST_PAD_PARENT (pad));
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

  if (!GST_PAD_CAPS (xviddec->sinkpad)) {
    GstPadTemplate *templ = gst_static_pad_template_get (&src_template);

    return gst_caps_copy (gst_pad_template_get_caps (templ));
  }

  caps = gst_caps_new_empty ();
  for (i = 0; csp[i] != 0; i++) {
    GstCaps *one = gst_xvid_csp_to_caps (csp[i], xviddec->width,
        xviddec->height, xviddec->fps, xviddec->par);

    gst_caps_append (caps, one);
  }

  return caps;
}


static GstPadLinkReturn
gst_xviddec_src_link (GstPad * pad, const GstCaps * vscaps)
{
  GstXvidDec *xviddec = GST_XVIDDEC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (vscaps, 0);

  if (!GST_PAD_CAPS (xviddec->sinkpad))
    return GST_PAD_LINK_DELAYED;

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset (xviddec);
  }
  xviddec->csp = gst_xvid_structure_to_csp (structure);

  if (xviddec->csp < 0)
    return GST_PAD_LINK_REFUSED;

  if (!gst_xviddec_setup (xviddec))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}
#endif

static gboolean
gst_xviddec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstXvidDec *xviddec = GST_XVIDDEC (GST_PAD_PARENT (pad));
  GstStructure *structure;
  const GValue *val;

  GST_DEBUG ("setcaps called");

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset (xviddec);
  }

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &xviddec->width);
  gst_structure_get_int (structure, "height", &xviddec->height);

  /* perhaps some fps info */
  val = gst_structure_get_value (structure, "framerate");
  if ((val != NULL) && GST_VALUE_HOLDS_FRACTION (val)) {
    xviddec->fps_n = gst_value_get_fraction_numerator (val);
    xviddec->fps_d = gst_value_get_fraction_denominator (val);
  } else {
    xviddec->fps_n = -1;
    xviddec->fps_d = 1;
  }

  /* perhaps some par info */
  val = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if ((val != NULL) && GST_VALUE_HOLDS_FRACTION (val)) {
    xviddec->par_n = gst_value_get_fraction_numerator (val);
    xviddec->par_d = gst_value_get_fraction_denominator (val);
  } else {
    xviddec->par_n = 1;
    xviddec->par_d = 1;
  }

  if (gst_xviddec_setup (xviddec)) {
    GstCaps *allowed_caps;

    /* we try to find the preferred/accept csp */
    allowed_caps = gst_pad_get_allowed_caps (xviddec->srcpad);
    if (!allowed_caps) {
      GST_DEBUG_OBJECT (xviddec, "... but no peer, using template caps");
      /* need to copy because get_allowed_caps returns a ref,
         and get_pad_template_caps doesn't */
      allowed_caps =
          gst_caps_copy (gst_pad_get_pad_template_caps (xviddec->srcpad));
    }
    /* pick the first one ... */
    structure = gst_caps_get_structure (allowed_caps, 0);
    val = gst_structure_get_value (structure, "format");
    if (G_VALUE_TYPE (val) == GST_TYPE_LIST) {
      GValue temp = { 0 };
      gst_value_init_and_copy (&temp, gst_value_list_get_value (val, 0));
      gst_structure_set_value (structure, "format", &temp);
      g_value_unset (&temp);
    }

    /* ... and use its info to get the csp */
    xviddec->csp = gst_xvid_structure_to_csp (structure);
    if (xviddec->csp == -1) {
      gchar *sstr = gst_structure_to_string (structure);

      GST_INFO_OBJECT (xviddec,
          "failed to decide upon csp from caps %s, trying I420", sstr);
      g_free (sstr);
      xviddec->csp = XVID_CSP_I420;
    }
    gst_caps_unref (allowed_caps);

    return gst_xviddec_negotiate (xviddec, NULL);

  } else                        /* setup did not work out */
    return FALSE;
}

static GstStateChangeReturn
gst_xviddec_change_state (GstElement * element, GstStateChange transition)
{
  GstXvidDec *xviddec = GST_XVIDDEC (element);
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
      gst_xviddec_flush_buffers (xviddec, FALSE);
      if (xviddec->handle) {
        gst_xviddec_unset (xviddec);
      }
      gst_xviddec_reset (xviddec);
      break;
    default:
      break;
  }

done:
  return ret;
}
