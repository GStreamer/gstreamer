/* GStreamer xvid decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
GstElementDetails gst_xviddec_details =
GST_ELEMENT_DETAILS ("XviD video decoder",
    "Codec/Decoder/Video",
    "XviD decoder based on xvidcore",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-xvid, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [0/1, MAX]")
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

static void gst_xviddec_base_init (GstXvidDecClass * klass);
static void gst_xviddec_class_init (GstXvidDecClass * klass);
static void gst_xviddec_init (GstXvidDec * xviddec);
static GstFlowReturn gst_xviddec_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_xviddec_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_xviddec_negotiate (GstXvidDec * xviddec);
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
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_xviddec_change_state;
}

static void
gst_xviddec_init (GstXvidDec * xviddec)
{
  gst_xvid_init ();

  /* create the sink pad */
  xviddec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (xviddec), xviddec->sinkpad);
  gst_pad_set_chain_function (xviddec->sinkpad, gst_xviddec_chain);
  gst_pad_set_setcaps_function (xviddec->sinkpad, gst_xviddec_setcaps);

  /* create the src pad */
  xviddec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (xviddec), xviddec->srcpad);
  gst_pad_use_fixed_caps (xviddec->srcpad);

  /* size, etc. */
  xviddec->width = xviddec->height = xviddec->csp = -1;

  /* set xvid handle to NULL */
  xviddec->handle = NULL;
}


static void
gst_xviddec_unset (GstXvidDec * xviddec)
{
  /* unref this instance */
  xvid_decore (xviddec->handle, XVID_DEC_DESTROY, NULL, NULL);
  xviddec->handle = NULL;
}


static gboolean
gst_xviddec_setup (GstXvidDec * xviddec)
{
  xvid_dec_create_t xdec;
  int ret;

  /* initialise parameters, see xvid documentation */
  gst_xvid_init_struct (xdec);
  xdec.width = xviddec->width;
  xdec.height = xviddec->height;
  xdec.handle = NULL;

  if ((ret = xvid_decore (NULL, XVID_DEC_CREATE, &xdec, NULL)) < 0) {
    GST_ELEMENT_ERROR (xviddec, LIBRARY, SETTINGS, (NULL),
        ("Setting parameters %dx%d@%d failed: %s (%d)",
            xviddec->width, xviddec->height, xviddec->csp,
            gst_xvid_error (ret), ret));
    return FALSE;
  }

  xviddec->handle = xdec.handle;
  return TRUE;
}

static GstFlowReturn
gst_xviddec_chain (GstPad * pad, GstBuffer * buf)
{
  GstXvidDec *xviddec = GST_XVIDDEC (gst_pad_get_parent (pad));
  GstBuffer *outbuf = NULL;
  xvid_dec_frame_t xframe;
  GstFlowReturn ret = GST_FLOW_OK;
  guint bufsize;
  int error = 0;

  if (xviddec->handle == NULL) {
    if (!gst_xviddec_negotiate (xviddec))
      goto not_negotiated;
  }

  bufsize = (xviddec->width * xviddec->height * xviddec->bpp / 8);

  outbuf = gst_buffer_new_and_alloc (bufsize);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
  GST_BUFFER_SIZE (outbuf) = bufsize;

  /* decode and so ... */
  gst_xvid_init_struct (xframe);
  xframe.general = XVID_LOWDELAY;
  xframe.bitstream = (void *) GST_BUFFER_DATA (buf);
  xframe.length = GST_BUFFER_SIZE (buf);
  xframe.output.csp = xviddec->csp;
  if (xviddec->width == xviddec->stride) {
    xframe.output.plane[0] = GST_BUFFER_DATA (outbuf);
    xframe.output.plane[1] =
        xframe.output.plane[0] + (xviddec->width * xviddec->height);
    xframe.output.plane[2] =
        xframe.output.plane[1] + (xviddec->width * xviddec->height / 4);
    xframe.output.stride[0] = xviddec->width;
    xframe.output.stride[1] = xviddec->width / 2;
    xframe.output.stride[2] = xviddec->width / 2;
  } else {
    xframe.output.plane[0] = GST_BUFFER_DATA (outbuf);
    xframe.output.stride[0] = xviddec->stride;
  }

  if ((error =
          xvid_decore (xviddec->handle, XVID_DEC_DECODE, &xframe, NULL)) < 0) {
    goto not_decoding;
  }

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (xviddec->srcpad));
  ret = gst_pad_push (xviddec->srcpad, outbuf);

  goto cleanup;

not_negotiated:
  {
    GST_ELEMENT_ERROR (xviddec, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto cleanup;
  }

not_decoding:
  {
    GST_ELEMENT_ERROR (xviddec, STREAM, DECODE, (NULL),
        ("Error decoding xvid frame: %s (%d)\n", gst_xvid_error (error),
            error));
    gst_buffer_unref (outbuf);
    ret = GST_FLOW_ERROR;
    goto cleanup;
  }

cleanup:

  gst_buffer_unref (buf);
  gst_object_unref (xviddec);
  return ret;

}

static gboolean
gst_xviddec_negotiate (GstXvidDec * xviddec)
{
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

  for (i = 0; csp[i] != 0; i++) {
    GstCaps *one = gst_xvid_csp_to_caps (csp[i], xviddec->width,
        xviddec->height, xviddec->fps_n, xviddec->fps_d);

    if (one) {

      if (gst_pad_set_caps (xviddec->srcpad, one)) {
        GstStructure *structure = gst_caps_get_structure (one, 0);

        xviddec->csp = gst_xvid_structure_to_csp (structure, xviddec->width,
            &xviddec->stride, &xviddec->bpp);

        if (xviddec->csp < 0) {
          return FALSE;
        }

        break;
      }

      gst_caps_unref (one);

    }

  }

  gst_xviddec_setup (xviddec);
  return TRUE;
}

static gboolean
gst_xviddec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstXvidDec *xviddec = GST_XVIDDEC (gst_pad_get_parent (pad));
  GstStructure *structure;
  const GValue *fps;
  gboolean ret = FALSE;

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset (xviddec);
  }

  if (!gst_pad_set_caps (xviddec->srcpad, caps)) {
    ret = FALSE;
    goto done;
  }

  /* if we get here, we know the input is xvid. we
   * only need to bother with the output colorspace,
   * which the src_link function takes care of. */
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &xviddec->width);
  gst_structure_get_int (structure, "height", &xviddec->height);

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL) {
    xviddec->fps_n = gst_value_get_fraction_numerator (fps);
    xviddec->fps_d = gst_value_get_fraction_denominator (fps);
  } else {
    xviddec->fps_n = -1;
  }

  ret = gst_xviddec_negotiate (xviddec);

done:
  gst_object_unref (xviddec);

  return ret;

}

static GstStateChangeReturn
gst_xviddec_change_state (GstElement * element, GstStateChange transition)
{
  GstXvidDec *xviddec = GST_XVIDDEC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (xviddec->handle) {
        gst_xviddec_unset (xviddec);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
