/* GStreamer PNM decoder
 * Copyright (C) 2009 Lutz Mueller <lutz@users.sourceforge.net>
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
 * SECTION:element-pnmdec
 *
 * Decodes pnm images.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.pnm ! pnmdec ! ffmpegcolorspace ! autovideosink
 * ]| The above pipeline reads a pnm file and renders it to the screen.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpnmdec.h"
#include "gstpnmutils.h"

#include <gst/gstutils.h>
#include <gst/video/video.h>

#include <string.h>

static GstStaticPadTemplate gst_pnmdec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB "; "
        "video/x-raw-gray, width =" GST_VIDEO_SIZE_RANGE ", "
        "height =" GST_VIDEO_SIZE_RANGE ", framerate =" GST_VIDEO_FPS_RANGE ", "
        "bpp= (int) 8, depth= (int) 8"));

static GstStaticPadTemplate gst_pnmdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MIME_ALL));

GST_BOILERPLATE (GstPnmdec, gst_pnmdec, GstElement, GST_TYPE_ELEMENT);

static GstFlowReturn
gst_pnmdec_push (GstPnmdec * s, GstPad * src, GstBuffer * buf)
{
  /* Need to convert from PNM rowstride to GStreamer rowstride */
  if (s->mngr.info.width % 4 != 0) {
    guint i_rowstride;
    guint o_rowstride;
    GstBuffer *obuf;
    guint i;

    if (s->mngr.info.type == GST_PNM_TYPE_PIXMAP) {
      i_rowstride = 3 * s->mngr.info.width;
      o_rowstride = GST_ROUND_UP_4 (i_rowstride);
    } else {
      i_rowstride = s->mngr.info.width;
      o_rowstride = GST_ROUND_UP_4 (i_rowstride);
    }

    obuf = gst_buffer_new_and_alloc (o_rowstride * s->mngr.info.height);

    gst_buffer_copy_metadata (obuf, buf, GST_BUFFER_COPY_ALL);

    for (i = 0; i < s->mngr.info.height; i++)
      memcpy (GST_BUFFER_DATA (obuf) + i * o_rowstride,
          GST_BUFFER_DATA (buf) + i * i_rowstride, i_rowstride);

    gst_buffer_unref (buf);
    return gst_pad_push (src, obuf);
  } else {
    return gst_pad_push (src, buf);
  }
}

static GstFlowReturn
gst_pnmdec_chain_raw (GstPnmdec * s, GstPad * src, GstBuffer * buf)
{
  GstFlowReturn r = GST_FLOW_OK;
  GstBuffer *out;

  /* If we got the whole image, just push the buffer. */
  if (GST_BUFFER_SIZE (buf) == s->size) {
    memset (&s->mngr, 0, sizeof (GstPnmInfoMngr));
    s->size = 0;
    gst_buffer_set_caps (buf, GST_PAD_CAPS (src));
    return gst_pnmdec_push (s, src, buf);
  }

  /* We didn't get the whole image. */
  if (!s->buf) {
    s->buf = buf;
  } else {
    out = gst_buffer_span (s->buf, 0, buf,
        GST_BUFFER_SIZE (s->buf) + GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);
    gst_buffer_unref (s->buf);
    s->buf = out;
  }
  if (!s->buf)
    return GST_FLOW_ERROR;

  /* Do we now have the full image? If yes, push. */
  if (GST_BUFFER_SIZE (s->buf) == s->size) {
    gst_buffer_set_caps (s->buf, GST_PAD_CAPS (src));
    r = gst_pnmdec_push (s, src, s->buf);
    s->buf = NULL;
    memset (&s->mngr, 0, sizeof (GstPnmInfoMngr));
    s->size = 0;
  }

  return r;
}

static GstFlowReturn
gst_pnmdec_chain_ascii (GstPnmdec * s, GstPad * src, GstBuffer * buf)
{
  GScanner *scanner;
  GstBuffer *out;
  guint i = 0;
  gchar *b = (gchar *) GST_BUFFER_DATA (buf);
  guint bs = GST_BUFFER_SIZE (buf);
  guint target = s->size - (s->buf ? GST_BUFFER_SIZE (s->buf) : 0);

  if (!bs) {
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  if (s->last_byte) {
    while (*b >= '0' && *b <= '9') {
      s->last_byte = 10 * s->last_byte + *b - '0';
      b++;
      if (!--bs) {
        gst_buffer_unref (buf);
        return GST_FLOW_OK;
      }
    }
    if (s->last_byte > 255) {
      gst_buffer_unref (buf);
      GST_DEBUG_OBJECT (s, "Corrupt ASCII encoded PNM file.");
      return GST_FLOW_ERROR;
    }
  }

  out = gst_buffer_new_and_alloc (target);

  if (s->last_byte) {
    GST_BUFFER_DATA (out)[i++] = s->last_byte;
    s->last_byte = 0;
  }

  scanner = g_scanner_new (NULL);
  g_scanner_input_text (scanner, b, bs);
  while (!g_scanner_eof (scanner)) {
    switch (g_scanner_get_next_token (scanner)) {
      case G_TOKEN_INT:
        if (i == target) {
          GST_DEBUG_OBJECT (s, "PNM file contains too much data.");
          gst_buffer_unref (buf);
          gst_buffer_unref (out);
          return GST_FLOW_ERROR;
        }
        GST_BUFFER_DATA (out)[i++] = scanner->value.v_int;
        break;
      default:
        /* Should we care? */ ;
    }
  }
  g_scanner_destroy (scanner);

  /* If we didn't get the whole image, handle the last byte with care. */
  if (i && i < target && b[bs - 1] > '0' && b[bs - 1] <= '9')
    s->last_byte = GST_BUFFER_DATA (out)[--i];

  gst_buffer_unref (buf);
  if (!i) {
    gst_buffer_unref (out);
    return GST_FLOW_OK;
  }

  GST_BUFFER_SIZE (out) = i;
  return gst_pnmdec_chain_raw (s, src, out);
}

static GstFlowReturn
gst_pnmdec_chain (GstPad * pad, GstBuffer * data)
{
  GstPnmdec *s = GST_PNMDEC (gst_pad_get_parent (pad));
  GstPad *src = gst_element_get_static_pad (GST_ELEMENT (s), "src");
  GstCaps *caps = NULL;
  GstFlowReturn r = GST_FLOW_OK;
  guint offset = 0;

  if (s->mngr.info.fields != GST_PNM_INFO_FIELDS_ALL) {
    switch (gst_pnm_info_mngr_scan (&s->mngr, GST_BUFFER_DATA (data),
            GST_BUFFER_SIZE (data))) {
      case GST_PNM_INFO_MNGR_RESULT_FAILED:
        gst_buffer_unref (data);
        r = GST_FLOW_ERROR;
        goto out;
      case GST_PNM_INFO_MNGR_RESULT_READING:
        gst_buffer_unref (data);
        r = GST_FLOW_OK;
        goto out;
      case GST_PNM_INFO_MNGR_RESULT_FINISHED:
        offset = s->mngr.data_offset;
        caps = gst_caps_copy (gst_pad_get_pad_template_caps (src));
        switch (s->mngr.info.type) {
          case GST_PNM_TYPE_BITMAP:
            GST_DEBUG_OBJECT (s, "FIXME: BITMAP format not implemented!");
            gst_caps_unref (caps);
            gst_buffer_unref (data);
            r = GST_FLOW_ERROR;
            goto out;
          case GST_PNM_TYPE_GRAYMAP:
            gst_caps_remove_structure (caps, 0);
            s->size = s->mngr.info.width * s->mngr.info.height * 1;
            break;
          case GST_PNM_TYPE_PIXMAP:
            gst_caps_remove_structure (caps, 1);
            s->size = s->mngr.info.width * s->mngr.info.height * 3;
            break;
        }
        gst_caps_set_simple (caps,
            "width", G_TYPE_INT, s->mngr.info.width,
            "height", G_TYPE_INT, s->mngr.info.height, "framerate",
            GST_TYPE_FRACTION, 0, 1, NULL);
        if (!gst_pad_set_caps (src, caps)) {
          gst_caps_unref (caps);
          gst_buffer_unref (data);
          r = GST_FLOW_ERROR;
          goto out;
        }
        gst_caps_unref (caps);
    }
  }

  if (offset == GST_BUFFER_SIZE (data)) {
    gst_buffer_unref (data);
    r = GST_FLOW_OK;
    goto out;
  }

  if (offset) {
    GstBuffer *buf = gst_buffer_create_sub (data, offset,
        GST_BUFFER_SIZE (data) - offset);
    gst_buffer_unref (data);
    data = buf;
  }

  if (s->mngr.info.encoding == GST_PNM_ENCODING_ASCII)
    r = gst_pnmdec_chain_ascii (s, src, data);
  else
    r = gst_pnmdec_chain_raw (s, src, data);

out:
  gst_object_unref (src);
  gst_object_unref (s);

  return r;
}

static void
gst_pnmdec_finalize (GObject * object)
{
  GstPnmdec *dec = GST_PNMDEC (object);

  if (dec->buf) {
    gst_buffer_unref (dec->buf);
    dec->buf = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pnmdec_init (GstPnmdec * s, GstPnmdecClass * klass)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_static_template (&gst_pnmdec_sink_pad_template, "sink");
  gst_pad_set_chain_function (pad, gst_pnmdec_chain);
  gst_element_add_pad (GST_ELEMENT (s), pad);

  pad = gst_pad_new_from_static_template (&gst_pnmdec_src_pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (s), pad);
}

static void
gst_pnmdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_pnmdec_sink_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_pnmdec_src_pad_template);
  gst_element_class_set_details_simple (element_class, "PNM image decoder",
      "Codec/Decoder/Image",
      "Decodes images in portable pixmap/graymap/bitmap/anymamp (PNM) format",
      "Lutz Mueller <lutz@users.sourceforge.net>");
}

static void
gst_pnmdec_class_init (GstPnmdecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_pnmdec_finalize;
}
