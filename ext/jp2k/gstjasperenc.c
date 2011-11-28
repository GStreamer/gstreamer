/* GStreamer Jasper based j2k image encoder
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
 * SECTION:element-jasperenc
 *
 * Encodes video to jpeg2000 images.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include <jasper/jasper.h>

#include "gstjasperenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_jasper_enc_debug);
#define GST_CAT_DEFAULT gst_jasper_enc_debug

enum
{
  ARG_0,
};

static GstStaticPadTemplate gst_jasper_enc_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB "; " GST_VIDEO_CAPS_BGR "; "
        GST_VIDEO_CAPS_RGBx "; " GST_VIDEO_CAPS_xRGB "; "
        GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_xBGR "; "
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, v308 }"))
    );

static GstStaticPadTemplate gst_jasper_enc_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-j2c, width = " GST_VIDEO_SIZE_RANGE ", height = "
        GST_VIDEO_SIZE_RANGE ", fourcc = (GstFourcc) { sRGB, sYUV },"
        "framerate = " GST_VIDEO_FPS_RANGE ", " "fields = (int) 1; "
        "image/x-jpc, width = " GST_VIDEO_SIZE_RANGE ", height = "
        GST_VIDEO_SIZE_RANGE ", fourcc = (GstFourcc) { sRGB, sYUV },"
        "framerate = " GST_VIDEO_FPS_RANGE ", " "fields = (int) 1; "
        "image/jp2")
    );

static void gst_jasper_enc_reset (GstJasperEnc * enc);
static GstStateChangeReturn gst_jasper_enc_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_jasper_enc_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_jasper_enc_chain (GstPad * pad, GstBuffer * buffer);

/* minor trick:
 * keep original naming but use unique name here for a happy type system
 */

typedef GstJasperEnc GstJp2kEnc;
typedef GstJasperEncClass GstJp2kEncClass;

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstJp2kEnc, gst_jasper_enc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_jasper_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_jasper_enc_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_jasper_enc_sink_template);
  gst_element_class_set_details_simple (element_class,
      "Jasper JPEG2000 image encoder", "Codec/Encoder/Image",
      "Encodes video to JPEG2000 using jasper",
      "Mark Nauwelaerts <mnauw@users.sf.net>");
}

/* initialize the plugin's class */
static void
gst_jasper_enc_class_init (GstJasperEncClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_jasper_enc_debug, "jp2kenc", 0,
      "Jasper JPEG2000 encoder");

  /* FIXME add some encoder properties */

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_jasper_enc_change_state);
}

static void
gst_jasper_enc_init (GstJasperEnc * enc, GstJasperEncClass * klass)
{
  enc->sinkpad =
      gst_pad_new_from_static_template (&gst_jasper_enc_sink_template, "sink");
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jasper_enc_sink_setcaps));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jasper_enc_chain));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_static_template (&gst_jasper_enc_src_template, "src");
  gst_pad_use_fixed_caps (enc->srcpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->buf = NULL;
  gst_jasper_enc_reset (enc);
}

static void
gst_jasper_enc_reset (GstJasperEnc * enc)
{
  if (enc->buf)
    g_free (enc->buf);
  enc->buf = NULL;
  if (enc->image)
    jas_image_destroy (enc->image);
  enc->image = NULL;
  enc->fmt = -1;
  enc->mode = GST_JP2ENC_MODE_J2C;
  enc->clrspc = JAS_CLRSPC_UNKNOWN;
  enc->format = GST_VIDEO_FORMAT_UNKNOWN;
}

static gboolean
gst_jasper_enc_set_src_caps (GstJasperEnc * enc)
{
  GstCaps *caps;
  guint32 fourcc;
  gboolean ret;
  GstCaps *peercaps;

  peercaps = gst_pad_peer_get_caps (enc->srcpad);
  if (peercaps) {
    guint i, n;

    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);
      const gchar *name = gst_structure_get_name (s);

      if (!strcmp (name, "image/x-j2c")) {
        enc->mode = GST_JP2ENC_MODE_J2C;
        break;
      } else if (!strcmp (name, "image/x-jpc")) {
        enc->mode = GST_JP2ENC_MODE_JPC;
        break;
      } else if (!strcmp (name, "image/jp2")) {
        enc->mode = GST_JP2ENC_MODE_JP2;
        break;
      }
    }
    gst_caps_unref (peercaps);
  }

  /* enumerated colourspace */
  if (gst_video_format_is_rgb (enc->format)) {
    fourcc = GST_MAKE_FOURCC ('s', 'R', 'G', 'B');
  } else {
    fourcc = GST_MAKE_FOURCC ('s', 'Y', 'U', 'V');
  }

  switch (enc->mode) {
    case GST_JP2ENC_MODE_J2C:
      caps =
          gst_caps_new_simple ("image/x-j2c", "width", G_TYPE_INT, enc->width,
          "height", G_TYPE_INT, enc->height, "fourcc", GST_TYPE_FOURCC, fourcc,
          NULL);
      break;
    case GST_JP2ENC_MODE_JPC:
      caps =
          gst_caps_new_simple ("image/x-jpc", "width", G_TYPE_INT, enc->width,
          "height", G_TYPE_INT, enc->height, "fourcc", GST_TYPE_FOURCC, fourcc,
          NULL);
      break;
    case GST_JP2ENC_MODE_JP2:
      caps = gst_caps_new_simple ("image/jp2", "width", G_TYPE_INT, enc->width,
          "height", G_TYPE_INT, enc->height, "fourcc", GST_TYPE_FOURCC, fourcc,
          NULL);
      break;
    default:
      g_assert_not_reached ();
  }


  if (enc->fps_den > 0)
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION, enc->fps_num, enc->fps_den, NULL);
  if (enc->par_den > 0)
    gst_caps_set_simple (caps,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, enc->par_num, enc->par_den,
        NULL);

  ret = gst_pad_set_caps (enc->srcpad, caps);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
gst_jasper_enc_init_encoder (GstJasperEnc * enc)
{
  jas_image_cmptparm_t param[GST_JASPER_ENC_MAX_COMPONENT];
  gint i;

  switch (enc->mode) {
    case GST_JP2ENC_MODE_J2C:
    case GST_JP2ENC_MODE_JPC:
      enc->fmt = jas_image_strtofmt ((char *) "jpc");
      break;
    case GST_JP2ENC_MODE_JP2:
      enc->fmt = jas_image_strtofmt ((char *) "jp2");
      break;
  }

  if (gst_video_format_is_rgb (enc->format))
    enc->clrspc = JAS_CLRSPC_SRGB;
  else
    enc->clrspc = JAS_CLRSPC_SYCBCR;

  if (enc->buf) {
    g_free (enc->buf);
    enc->buf = NULL;
  }
  enc->buf = g_new0 (glong, enc->width);

  if (enc->image) {
    jas_image_destroy (enc->image);
    enc->image = NULL;
  }

  for (i = 0; i < enc->channels; ++i) {
    param[i].tlx = 0;
    param[i].tly = 0;
    param[i].prec = 8;
    param[i].sgnd = 0;
    param[i].height = enc->cheight[i];
    param[i].width = enc->cwidth[i];
    param[i].hstep = enc->height / param[i].height;
    param[i].vstep = enc->width / param[i].width;
  }

  if (!(enc->image = jas_image_create (enc->channels, param, enc->clrspc)))
    return FALSE;

  return TRUE;
}

static gboolean
gst_jasper_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstJasperEnc *enc;
  GstVideoFormat format;
  gint width, height;
  gint fps_num, fps_den;
  gint par_num, par_den;
  gint i;

  enc = GST_JASPER_ENC (GST_PAD_PARENT (pad));

  /* get info from caps */
  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    goto refuse_caps;
  /* optional; pass along if present */
  fps_num = fps_den = -1;
  par_num = par_den = -1;
  gst_video_parse_caps_framerate (caps, &fps_num, &fps_den);
  gst_video_parse_caps_pixel_aspect_ratio (caps, &par_num, &par_den);

  if (width == enc->width && height == enc->height && enc->format == format
      && fps_num == enc->fps_num && fps_den == enc->fps_den
      && par_num == enc->par_num && par_den == enc->par_den)
    return TRUE;

  /* store input description */
  enc->format = format;
  enc->width = width;
  enc->height = height;
  enc->fps_num = fps_num;
  enc->fps_den = fps_den;
  enc->par_num = par_num;
  enc->par_den = par_den;

  /* prepare a cached image description  */
  enc->channels = 3 + (gst_video_format_has_alpha (format) ? 1 : 0);
  for (i = 0; i < enc->channels; ++i) {
    enc->cwidth[i] = gst_video_format_get_component_width (format, i, width);
    enc->cheight[i] = gst_video_format_get_component_height (format, i, height);
    enc->offset[i] = gst_video_format_get_component_offset (format, i, width,
        height);
    enc->stride[i] = gst_video_format_get_row_stride (format, i, width);
    enc->inc[i] = gst_video_format_get_pixel_stride (format, i);
  }

  if (!gst_jasper_enc_set_src_caps (enc))
    goto setcaps_failed;
  if (!gst_jasper_enc_init_encoder (enc))
    goto setup_failed;

  return TRUE;

  /* ERRORS */
setup_failed:
  {
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, (NULL), (NULL));
    return FALSE;
  }
setcaps_failed:
  {
    GST_WARNING_OBJECT (enc, "Setting src caps failed");
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, (NULL), (NULL));
    return FALSE;
  }
refuse_caps:
  {
    GST_WARNING_OBJECT (enc, "refused caps %" GST_PTR_FORMAT, caps);
    gst_object_unref (enc);
    return FALSE;
  }
}

static GstFlowReturn
gst_jasper_enc_get_data (GstJasperEnc * enc, guint8 * data, GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  jas_stream_t *stream = NULL;
  gint i;
  guint size, boxsize;

  g_return_val_if_fail (outbuf != NULL, GST_FLOW_ERROR);

  *outbuf = NULL;

  boxsize = (enc->mode == GST_JP2ENC_MODE_J2C) ? 8 : 0;

  if (!(stream = jas_stream_memopen (NULL, 0)))
    goto fail_stream;

  for (i = 0; i < enc->channels; ++i) {
    gint x, y, cwidth, cheight, inc, stride, cmpt;
    guint8 *row_pix, *in_pix;
    glong *tb;

    cmpt = i;
    inc = enc->inc[i];
    stride = enc->stride[i];
    cheight = enc->cheight[cmpt];
    cwidth = enc->cwidth[cmpt];

    GST_LOG_OBJECT (enc,
        "write component %d<=%d, size %dx%d, offset %d, inc %d, stride %d",
        i, cmpt, cwidth, cheight, enc->offset[i], inc, stride);

    row_pix = data + enc->offset[i];

    for (y = 0; y < cheight; y++) {
      in_pix = row_pix;
      tb = enc->buf;
      for (x = 0; x < cwidth; x++) {
        *tb = *in_pix;
        in_pix += inc;
        tb++;
      }
      if (jas_image_writecmpt2 (enc->image, cmpt, 0, y, cwidth, 1, enc->buf))
        goto fail_image;
      row_pix += stride;
    }
  }

  GST_LOG_OBJECT (enc, "all components written");

  if (jas_image_encode (enc->image, stream, enc->fmt, (char *) "sop"))
    goto fail_encode;

  GST_LOG_OBJECT (enc, "image encoded");

  size = jas_stream_length (stream);
  ret = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
      GST_BUFFER_OFFSET_NONE, size + boxsize, GST_PAD_CAPS (enc->srcpad),
      outbuf);

  if (ret != GST_FLOW_OK)
    goto no_buffer;

  data = GST_BUFFER_DATA (*outbuf);
  if (jas_stream_flush (stream) ||
      jas_stream_rewind (stream) < 0 ||
      jas_stream_read (stream, data + boxsize, size) < size)
    goto fail_image_out;

  if (boxsize) {
    /* write atom prefix */
    GST_WRITE_UINT32_BE (data, size + 8);
    GST_WRITE_UINT32_LE (data + 4, GST_MAKE_FOURCC ('j', 'p', '2', 'c'));
  }

done:
  if (stream)
    jas_stream_close (stream);

  return ret;

  /* ERRORS */
fail_stream:
  {
    GST_DEBUG_OBJECT (enc, "Failed to create inputstream.");
    goto fail;
  }
fail_encode:
  {
    GST_DEBUG_OBJECT (enc, "Failed to encode image.");
    goto fail;
  }
fail_image:
  {
    GST_DEBUG_OBJECT (enc, "Failed to process input image.");
    goto fail;
  }
fail_image_out:
  {
    GST_DEBUG_OBJECT (enc, "Failed to process encoded image.");
    goto fail;
  }
fail:
  {
    if (*outbuf)
      gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }
no_buffer:
  {
    GST_DEBUG_OBJECT (enc, "Failed to create outbuffer - %s",
        gst_flow_get_name (ret));
    goto done;
  }
}

static GstFlowReturn
gst_jasper_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstJasperEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = NULL;
  guint8 *data;
  gboolean discont = FALSE;

  enc = GST_JASPER_ENC (gst_pad_get_parent (pad));

  if (enc->fmt < 0)
    goto not_negotiated;

  GST_LOG_OBJECT (enc, "buffer with ts: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  discont = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT);

  /* now really feed the data to encoder */
  data = GST_BUFFER_DATA (buf);
  ret = gst_jasper_enc_get_data (enc, data, &outbuf);

  if (outbuf) {
    gst_buffer_copy_metadata (outbuf, buf, GST_BUFFER_COPY_TIMESTAMPS);
    if (discont)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  }

  if (ret == GST_FLOW_OK && outbuf)
    ret = gst_pad_push (enc->srcpad, outbuf);

done:
  gst_buffer_unref (buf);
  gst_object_unref (enc);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static GstStateChangeReturn
gst_jasper_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstJasperEnc *enc = GST_JASPER_ENC (element);

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
      gst_jasper_enc_reset (enc);
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
    GST_ELEMENT_ERROR (enc, LIBRARY, INIT, (NULL), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
}
