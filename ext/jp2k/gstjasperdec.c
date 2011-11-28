/* GStreamer Jasper based j2k image decoder
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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

/**
 * SECTION:element-jasperdec
 *
 * Decodes jpeg2000 images.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include <jasper/jasper.h>

#include "gstjasperdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_jasper_dec_debug);
#define GST_CAT_DEFAULT gst_jasper_dec_debug

enum
{
  ARG_0,
};

/* FIXME 0.11: Use a single caps name for jpeg2000 codestreams
 * and drop the "boxed" variant
 */

static GstStaticPadTemplate gst_jasper_dec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-j2c, "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "fields = (int) 1; "
        "image/x-jpc, "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "fields = (int) 1; " "image/jp2")
    );

static GstStaticPadTemplate gst_jasper_dec_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB "; " GST_VIDEO_CAPS_BGR "; "
        GST_VIDEO_CAPS_RGBx "; " GST_VIDEO_CAPS_xRGB "; "
        GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_xBGR "; "
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, Y41B, Y42B, v308 }"))
    );

static void gst_jasper_dec_reset (GstJasperDec * dec);
static GstStateChangeReturn gst_jasper_dec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_jasper_dec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_jasper_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_jasper_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_jasper_dec_sink_event (GstPad * pad, GstEvent * event);
static void gst_jasper_dec_update_qos (GstJasperDec * dec, gdouble proportion,
    GstClockTime time);
static void gst_jasper_dec_reset_qos (GstJasperDec * dec);
static void gst_jasper_dec_read_qos (GstJasperDec * dec, gdouble * proportion,
    GstClockTime * time);

/* minor trick:
 * keep original naming but use unique name here for a happy type system
 */

typedef GstJasperDec GstJp2kDec;
typedef GstJasperDecClass GstJp2kDecClass;

GST_BOILERPLATE (GstJp2kDec, gst_jasper_dec, GstElement, GST_TYPE_ELEMENT);

static void
gst_jasper_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_jasper_dec_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_jasper_dec_sink_template);
  gst_element_class_set_details_simple (element_class,
      "Jasper JPEG2000 image decoder", "Codec/Decoder/Image",
      "Decodes JPEG2000 encoded images using jasper",
      "Mark Nauwelaerts <mnauw@users.sf.net>");
}

/* initialize the plugin's class */
static void
gst_jasper_dec_class_init (GstJasperDecClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_jasper_dec_debug, "jp2kdec", 0,
      "Jasper JPEG2000 decoder");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_jasper_dec_change_state);
}

static void
gst_jasper_dec_init (GstJasperDec * dec, GstJasperDecClass * klass)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&gst_jasper_dec_sink_template, "sink");
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jasper_dec_sink_setcaps));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jasper_dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jasper_dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&gst_jasper_dec_src_template, "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_jasper_dec_src_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->codec_data = NULL;
  dec->buf = NULL;
  gst_jasper_dec_reset (dec);
}

static void
gst_jasper_dec_reset (GstJasperDec * dec)
{
  if (dec->codec_data)
    gst_buffer_unref (dec->codec_data);
  dec->codec_data = NULL;
  if (dec->buf)
    g_free (dec->buf);
  dec->buf = NULL;
  dec->fmt = -1;
  dec->clrspc = JAS_CLRSPC_UNKNOWN;
  dec->format = GST_VIDEO_FORMAT_UNKNOWN;
  gst_jasper_dec_reset_qos (dec);
  gst_segment_init (&dec->segment, GST_FORMAT_TIME);
  dec->discont = TRUE;
}

static gboolean
gst_jasper_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstJasperDec *dec;
  const GValue *framerate;
  GstStructure *s;
  const gchar *mimetype;
  guint32 fourcc;

  dec = GST_JASPER_DEC (GST_PAD_PARENT (pad));
  s = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (s);

  /* reset negotiation */
  dec->fmt = -1;
  dec->strip = 0;
  dec->format = GST_VIDEO_FORMAT_UNKNOWN;
  if (dec->codec_data) {
    gst_buffer_unref (dec->codec_data);
    dec->codec_data = NULL;
  }

  if (!strcmp (mimetype, "image/x-j2c") || !strcmp (mimetype, "image/x-jpc")) {
    const GValue *codec_data;
    gint fields;

    /* we only handle single field, packetized input */
    if (gst_structure_get_value (s, "framerate") == NULL)
      goto refuse_caps;
    if (gst_structure_get_int (s, "fields", &fields) && fields != 1)
      goto refuse_caps;

    if (!gst_structure_get_fourcc (s, "fourcc", &fourcc))
      goto refuse_caps;
    switch (fourcc) {
      case GST_MAKE_FOURCC ('s', 'R', 'G', 'B'):
        dec->clrspc = JAS_CLRSPC_SRGB;
        break;
      case GST_MAKE_FOURCC ('s', 'Y', 'U', 'V'):
        dec->clrspc = JAS_CLRSPC_SYCBCR;
        break;
      default:
        goto refuse_caps;
        break;
    }

    dec->fmt = jas_image_strtofmt ((char *) "jpc");
    /* strip the j2c box stuff it is embedded in */
    if (!strcmp (mimetype, "image/x-jpc"))
      dec->strip = 0;
    else
      dec->strip = 8;

    codec_data = gst_structure_get_value (s, "codec_data");
    if (codec_data) {
      dec->codec_data = gst_value_get_buffer (codec_data);
      gst_buffer_ref (dec->codec_data);
    }
  } else if (!strcmp (mimetype, "image/jp2"))
    dec->fmt = jas_image_strtofmt ((char *) "jp2");

  if (dec->fmt < 0)
    goto refuse_caps;

  if ((framerate = gst_structure_get_value (s, "framerate")) != NULL) {
    dec->framerate_numerator = gst_value_get_fraction_numerator (framerate);
    dec->framerate_denominator = gst_value_get_fraction_denominator (framerate);
    GST_DEBUG_OBJECT (dec, "got framerate of %d/%d fps => packetized mode",
        dec->framerate_numerator, dec->framerate_denominator);
  } else {
    dec->framerate_numerator = 0;
    dec->framerate_denominator = 1;
    GST_DEBUG_OBJECT (dec, "no framerate, assuming single image");
  }

  return TRUE;

refuse_caps:
  {
    GST_WARNING_OBJECT (dec, "refused caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstFlowReturn
gst_jasper_dec_negotiate (GstJasperDec * dec, jas_image_t * image)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  gint width, height, channels;
  gint i, j;
  gboolean negotiate = FALSE;
  jas_clrspc_t clrspc;
  GstCaps *allowed_caps, *caps;

  width = jas_image_width (image);
  height = jas_image_height (image);
  channels = jas_image_numcmpts (image);

  GST_LOG_OBJECT (dec, "%d x %d, %d components", width, height, channels);

  /* jp2c bitstream has no real colour space info (kept in container),
   * so decoder may only pretend to know, where it really does not */
  if (!jas_clrspc_isunknown (dec->clrspc)) {
    clrspc = dec->clrspc;
    GST_DEBUG_OBJECT (dec, "forcing container supplied colour space %d",
        clrspc);
    jas_image_setclrspc (image, clrspc);
  } else
    clrspc = jas_image_clrspc (image);

  if (!width || !height || !channels || jas_clrspc_isunknown (clrspc))
    goto fail_image;

  if (dec->width != width || dec->height != height ||
      dec->channels != channels || dec->clrspc != clrspc)
    negotiate = TRUE;

  if (channels != 3)
    goto not_supported;

  for (i = 0; i < channels; i++) {
    gint cheight, cwidth, depth, sgnd;

    cheight = jas_image_cmptheight (image, i);
    cwidth = jas_image_cmptwidth (image, i);
    depth = jas_image_cmptprec (image, i);
    sgnd = jas_image_cmptsgnd (image, i);

    GST_LOG_OBJECT (dec, "image component %d, %dx%d, depth %d, sgnd %d", i,
        cwidth, cheight, depth, sgnd);

    if (depth != 8 || sgnd)
      goto not_supported;

    if (dec->cheight[i] != cheight || dec->cwidth[i] != cwidth) {
      dec->cheight[i] = cheight;
      dec->cwidth[i] = cwidth;
      negotiate = TRUE;
    }
  }

  if (!negotiate && dec->format != GST_VIDEO_FORMAT_UNKNOWN)
    goto done;

  /* clear and refresh to new state */
  flow_ret = GST_FLOW_NOT_NEGOTIATED;
  dec->format = GST_VIDEO_FORMAT_UNKNOWN;
  dec->width = width;
  dec->height = height;
  dec->channels = channels;

  /* retrieve allowed caps, and find the first one that reasonably maps
   * to the parameters of the colourspace */
  caps = gst_pad_get_allowed_caps (dec->srcpad);
  if (!caps) {
    GST_DEBUG_OBJECT (dec, "... but no peer, using template caps");
    /* need to copy because get_allowed_caps returns a ref,
       and get_pad_template_caps doesn't */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (dec->srcpad));
  }
  /* avoid lists of fourcc, etc */
  allowed_caps = gst_caps_normalize (caps);
  gst_caps_unref (caps);
  caps = NULL;
  GST_LOG_OBJECT (dec, "allowed source caps %" GST_PTR_FORMAT, allowed_caps);

  for (i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    GstVideoFormat format;
    gboolean ok;

    if (caps)
      gst_caps_unref (caps);
    caps = gst_caps_copy_nth (allowed_caps, i);
    /* sigh, ds and _parse_caps need fixed caps for parsing, fixate */
    gst_pad_fixate_caps (dec->srcpad, caps);
    GST_LOG_OBJECT (dec, "checking caps %" GST_PTR_FORMAT, caps);
    if (!gst_video_format_parse_caps (caps, &format, NULL, NULL))
      continue;
    if (gst_video_format_is_rgb (format) &&
        jas_clrspc_fam (clrspc) == JAS_CLRSPC_FAM_RGB) {
      GST_DEBUG_OBJECT (dec, "trying RGB");
      if ((dec->cmpt[0] = jas_image_getcmptbytype (image,
                  JAS_IMAGE_CT_COLOR (JAS_CLRSPC_CHANIND_RGB_R))) < 0 ||
          (dec->cmpt[1] = jas_image_getcmptbytype (image,
                  JAS_IMAGE_CT_COLOR (JAS_CLRSPC_CHANIND_RGB_G))) < 0 ||
          (dec->cmpt[2] = jas_image_getcmptbytype (image,
                  JAS_IMAGE_CT_COLOR (JAS_CLRSPC_CHANIND_RGB_B))) < 0) {
        GST_DEBUG_OBJECT (dec, "missing RGB color component");
        continue;
      }
    } else if (gst_video_format_is_yuv (format) &&
        jas_clrspc_fam (clrspc) == JAS_CLRSPC_FAM_YCBCR) {
      GST_DEBUG_OBJECT (dec, "trying YUV");
      if ((dec->cmpt[0] = jas_image_getcmptbytype (image,
                  JAS_IMAGE_CT_COLOR (JAS_CLRSPC_CHANIND_YCBCR_Y))) < 0 ||
          (dec->cmpt[1] = jas_image_getcmptbytype (image,
                  JAS_IMAGE_CT_COLOR (JAS_CLRSPC_CHANIND_YCBCR_CB))) < 0 ||
          (dec->cmpt[2] = jas_image_getcmptbytype (image,
                  JAS_IMAGE_CT_COLOR (JAS_CLRSPC_CHANIND_YCBCR_CR))) < 0) {
        GST_DEBUG_OBJECT (dec, "missing YUV color component");
        continue;
      }
    } else
      continue;
    /* match format with validity checks */
    ok = TRUE;
    for (j = 0; j < channels; j++) {
      gint cmpt;

      cmpt = dec->cmpt[j];
      if (dec->cwidth[cmpt] != gst_video_format_get_component_width (format, j,
              width) ||
          dec->cheight[cmpt] != gst_video_format_get_component_height (format,
              j, height))
        ok = FALSE;
    }
    /* commit to this format */
    if (ok) {
      dec->format = format;
      break;
    }
  }

  if (caps)
    gst_caps_unref (caps);
  gst_caps_unref (allowed_caps);

  if (dec->format != GST_VIDEO_FORMAT_UNKNOWN) {
    /* cache some video format properties */
    for (j = 0; j < channels; ++j) {
      dec->offset[j] = gst_video_format_get_component_offset (dec->format, j,
          dec->width, dec->height);
      dec->inc[j] = gst_video_format_get_pixel_stride (dec->format, j);
      dec->stride[j] = gst_video_format_get_row_stride (dec->format, j,
          dec->width);
    }
    dec->image_size = gst_video_format_get_size (dec->format, width, height);
    dec->alpha = gst_video_format_has_alpha (dec->format);

    if (dec->buf)
      g_free (dec->buf);
    dec->buf = g_new0 (glong, dec->width);

    caps = gst_video_format_new_caps (dec->format, dec->width, dec->height,
        dec->framerate_numerator, dec->framerate_denominator, 1, 1);

    GST_DEBUG_OBJECT (dec, "Set format to %d, size to %dx%d", dec->format,
        dec->width, dec->height);

    if (!gst_pad_set_caps (dec->srcpad, caps))
      flow_ret = GST_FLOW_NOT_NEGOTIATED;
    else
      flow_ret = GST_FLOW_OK;

    gst_caps_unref (caps);
  }

done:
  return flow_ret;

  /* ERRORS */
fail_image:
  {
    GST_DEBUG_OBJECT (dec, "Failed to process decoded image.");
    flow_ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
not_supported:
  {
    GST_DEBUG_OBJECT (dec, "Decoded image has unsupported colour space.");
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Unsupported colorspace"));
    flow_ret = GST_FLOW_ERROR;
    goto done;
  }
}

static GstFlowReturn
gst_jasper_dec_get_picture (GstJasperDec * dec, guint8 * data,
    guint size, GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  jas_stream_t *stream = NULL;
  jas_image_t *image = NULL;
  gint i;

  g_return_val_if_fail (outbuf != NULL, GST_FLOW_ERROR);

  *outbuf = NULL;

  if (!(stream = jas_stream_memopen ((gpointer) data, size)))
    goto fail_stream;

  if (!(image = jas_image_decode (stream, dec->fmt, (char *) "")))
    goto fail_decode;

  ret = gst_jasper_dec_negotiate (dec, image);
  if (ret != GST_FLOW_OK)
    goto fail_negotiate;

  ret = gst_pad_alloc_buffer_and_set_caps (dec->srcpad,
      GST_BUFFER_OFFSET_NONE,
      dec->image_size, GST_PAD_CAPS (dec->srcpad), outbuf);

  if (ret != GST_FLOW_OK)
    goto no_buffer;

  if (dec->alpha)
    memset (GST_BUFFER_DATA (*outbuf), 0xff, dec->image_size);

  for (i = 0; i < dec->channels; ++i) {
    gint x, y, cwidth, cheight, inc, stride, cmpt;
    guint8 *row_pix, *out_pix;
    glong *tb;

    inc = dec->inc[i];
    stride = dec->stride[i];
    cmpt = dec->cmpt[i];
    cheight = dec->cheight[cmpt];
    cwidth = dec->cwidth[cmpt];

    GST_LOG_OBJECT (dec,
        "retrieve component %d<=%d, size %dx%d, offset %d, inc %d, stride %d",
        i, cmpt, cwidth, cheight, dec->offset[i], inc, stride);

    out_pix = GST_BUFFER_DATA (*outbuf) + dec->offset[i];

    for (y = 0; y < cheight; y++) {
      row_pix = out_pix;
      tb = dec->buf;
      if (jas_image_readcmpt2 (image, i, 0, y, cwidth, 1, dec->buf))
        goto fail_image;
      for (x = 0; x < cwidth; x++) {
        *out_pix = *tb;
        tb++;
        out_pix += inc;
      }
      out_pix = row_pix + stride;
    }
  }

  GST_LOG_OBJECT (dec, "all components retrieved");

done:
  if (image)
    jas_image_destroy (image);
  if (stream)
    jas_stream_close (stream);

  return ret;

  /* ERRORS */
fail_stream:
  {
    GST_DEBUG_OBJECT (dec, "Failed to create inputstream.");
    goto fail;
  }
fail_decode:
  {
    GST_DEBUG_OBJECT (dec, "Failed to decode image.");
    goto fail;
  }
fail_image:
  {
    GST_DEBUG_OBJECT (dec, "Failed to process decoded image.");
    goto fail;
  }
fail:
  {
    if (*outbuf)
      gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    GST_ELEMENT_WARNING (dec, STREAM, DECODE, (NULL), (NULL));
    ret = GST_FLOW_OK;
    goto done;
  }
no_buffer:
  {
    GST_DEBUG_OBJECT (dec, "Failed to create outbuffer - %s",
        gst_flow_get_name (ret));
    goto done;
  }
fail_negotiate:
  {
    GST_DEBUG_OBJECT (dec, "Failed to determine output caps.");
    goto done;
  }
}

/* Perform qos calculations before decoding the next frame. Returns TRUE if the
 * frame should be decoded, FALSE if the frame can be dropped entirely */
static gboolean
gst_jasper_dec_do_qos (GstJasperDec * dec, GstClockTime timestamp)
{
  GstClockTime qostime, earliest_time;
  gdouble proportion;

  /* no timestamp, can't do QoS => decode frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (dec, "invalid timestamp, can't do QoS, decode frame");
    return TRUE;
  }

  /* get latest QoS observation values */
  gst_jasper_dec_read_qos (dec, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => decode frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (dec, "no observation yet, decode frame");
    return TRUE;
  }

  /* qos is done on running time */
  qostime = gst_segment_to_running_time (&dec->segment, GST_FORMAT_TIME,
      timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (dec, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  if (qostime != GST_CLOCK_TIME_NONE && qostime <= earliest_time) {
    GST_DEBUG_OBJECT (dec, "we are late, drop frame");
    return FALSE;
  }

  GST_LOG_OBJECT (dec, "decode frame");
  return TRUE;
}

static GstFlowReturn
gst_jasper_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstJasperDec *dec;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime ts;
  GstBuffer *outbuf = NULL;
  guint8 *data;
  guint size;
  gboolean decode;

  dec = GST_JASPER_DEC (GST_PAD_PARENT (pad));

  if (dec->fmt < 0)
    goto not_negotiated;

  ts = GST_BUFFER_TIMESTAMP (buf);

  GST_LOG_OBJECT (dec, "buffer with ts: %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))
    dec->discont = TRUE;

  decode = gst_jasper_dec_do_qos (dec, ts);

  /* FIXME: do clipping */

  if (G_UNLIKELY (!decode)) {
    dec->discont = TRUE;
    goto done;
  }

  /* strip possible prefix */
  if (dec->strip) {
    GstBuffer *tmp;

    tmp = gst_buffer_create_sub (buf, dec->strip,
        GST_BUFFER_SIZE (buf) - dec->strip);
    gst_buffer_copy_metadata (tmp, buf, GST_BUFFER_COPY_TIMESTAMPS);
    gst_buffer_unref (buf);
    buf = tmp;
  }
  /* preprend possible codec_data */
  if (dec->codec_data) {
    GstBuffer *tmp;

    tmp = gst_buffer_merge (dec->codec_data, buf);
    gst_buffer_copy_metadata (tmp, buf, GST_BUFFER_COPY_TIMESTAMPS);
    gst_buffer_unref (buf);
    buf = tmp;
  }

  /* now really feed the data to decoder */
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  ret = gst_jasper_dec_get_picture (dec, data, size, &outbuf);

  if (outbuf) {
    gst_buffer_copy_metadata (outbuf, buf, GST_BUFFER_COPY_TIMESTAMPS);
    if (dec->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      dec->discont = FALSE;
    }

    if (ret == GST_FLOW_OK)
      ret = gst_pad_push (dec->srcpad, outbuf);
    else
      gst_buffer_unref (outbuf);
  }

done:
  gst_buffer_unref (buf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (dec, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static void
gst_jasper_dec_update_qos (GstJasperDec * dec, gdouble proportion,
    GstClockTime time)
{
  GST_OBJECT_LOCK (dec);
  dec->proportion = proportion;
  dec->earliest_time = time;
  GST_OBJECT_UNLOCK (dec);
}

static void
gst_jasper_dec_reset_qos (GstJasperDec * dec)
{
  gst_jasper_dec_update_qos (dec, 0.5, GST_CLOCK_TIME_NONE);
}

static void
gst_jasper_dec_read_qos (GstJasperDec * dec, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (dec);
  *proportion = dec->proportion;
  *time = dec->earliest_time;
  GST_OBJECT_UNLOCK (dec);
}

static gboolean
gst_jasper_dec_src_event (GstPad * pad, GstEvent * event)
{
  GstJasperDec *dec;
  gboolean res;

  dec = GST_JASPER_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      gst_jasper_dec_update_qos (dec, proportion, timestamp + diff);
      break;
    }
    default:
      break;
  }

  res = gst_pad_push_event (dec->sinkpad, event);

  gst_object_unref (dec);
  return res;
}

static gboolean
gst_jasper_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstJasperDec *dec;
  gboolean res = FALSE;

  dec = GST_JASPER_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_jasper_dec_reset_qos (dec);
      gst_segment_init (&dec->segment, GST_FORMAT_TIME);
      dec->discont = TRUE;
      break;
    case GST_EVENT_NEWSEGMENT:{
      gboolean update;
      GstFormat fmt;
      gint64 start, stop, time;
      gdouble rate, arate;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &fmt,
          &start, &stop, &time);

      switch (fmt) {
        case GST_FORMAT_TIME:
          /* great, our native segment format */
          break;
        case GST_FORMAT_BYTES:
          /* hmm .. */
          if (start != 0 || time != 0)
            goto invalid_bytes_segment;
          /* create bogus segment in TIME format, starting from 0 */
          gst_event_unref (event);
          fmt = GST_FORMAT_TIME;
          start = 0;
          stop = -1;
          time = 0;
          event = gst_event_new_new_segment (update, rate, fmt, start, stop,
              time);
          break;
        default:
          /* invalid format */
          goto invalid_format;
      }

      gst_segment_set_newsegment_full (&dec->segment, update, rate, arate,
          fmt, start, stop, time);

      GST_DEBUG_OBJECT (dec, "NEWSEGMENT %" GST_SEGMENT_FORMAT, &dec->segment);
      break;
    }
    default:
      break;
  }

  res = gst_pad_push_event (dec->srcpad, event);

done:

  gst_object_unref (dec);
  return res;

/* ERRORS */
invalid_format:
  {
    GST_WARNING_OBJECT (dec, "unknown format received in NEWSEGMENT event");
    gst_event_unref (event);
    goto done;
  }
invalid_bytes_segment:
  {
    GST_WARNING_OBJECT (dec, "can't handle NEWSEGMENT event in BYTES format "
        "with a non-0 start or non-0 time value");
    gst_event_unref (event);
    goto done;
  }
}

static GstStateChangeReturn
gst_jasper_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstJasperDec *dec = GST_JASPER_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (jas_init ())
        goto fail_init;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_jasper_dec_reset (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      jas_cleanup ();
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
fail_init:
  {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
}
