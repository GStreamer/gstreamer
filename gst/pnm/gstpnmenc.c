/* GStreamer PNM encoder
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-pnmenc
 *
 * Encodes pnm images. This plugin supports both raw and ASCII encoding.
 * To enable ASCII encoding, set the parameter ascii to TRUE. If you omit
 * the parameter or set it to FALSE, the output will be raw encoded.
 *
 * <refsect>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc num_buffers=1 ! videoconvert ! "video/x-raw,format=GRAY8" ! pnmenc ascii=true ! filesink location=test.pnm
 * ]| The above pipeline writes a test pnm file (ASCII encoding).
 * </refsect2>
 */

/*
 * FIXME: Port to GstVideoEncoder
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpnmenc.h"
#include "gstpnmutils.h"

#include <gst/gstutils.h>
#include <gst/video/video.h>

#include <string.h>

enum
{
  GST_PNMENC_PROP_0,
  GST_PNMENC_PROP_ASCII
      /* Add here. */
};

static GstStaticPadTemplate sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB") "; "
        GST_VIDEO_CAPS_MAKE ("GRAY8")));


static GstStaticPadTemplate src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MIME_ALL));

G_DEFINE_TYPE (GstPnmenc, gst_pnmenc, GST_TYPE_ELEMENT);

static void
gst_pnmenc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstPnmenc *s = GST_PNMENC (object);

  switch (prop_id) {
    case GST_PNMENC_PROP_ASCII:
      if (g_value_get_boolean (value))
        s->info.encoding = GST_PNM_ENCODING_ASCII;
      else
        s->info.encoding = GST_PNM_ENCODING_RAW;
      s->info.fields |= GST_PNM_INFO_FIELDS_ENCODING;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pnmenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPnmenc *s = GST_PNMENC (object);

  switch (prop_id) {
    case GST_PNMENC_PROP_ASCII:
      g_value_set_boolean (value, s->info.encoding == GST_PNM_ENCODING_ASCII);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_pnmenc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstPnmenc *s = GST_PNMENC (parent);
  GstFlowReturn r;
  gchar *header;
  GstBuffer *out;

  if (s->info.width == 0 || s->info.height == 0 || s->info.fields == 0)
    goto not_negotiated;

  /* Assumption: One buffer, one image. That is, always first write header. */
  header = g_strdup_printf ("P%i\n%i %i\n%i\n",
      s->info.type + 3 * (1 - s->info.encoding), s->info.width, s->info.height,
      s->info.max);
  out = gst_buffer_new_wrapped (header, strlen (header));
  if ((r = gst_pad_push (s->src, out)) != GST_FLOW_OK)
    goto out;

  /* Need to convert from GStreamer rowstride to PNM rowstride */
  if (s->info.width % 4 != 0) {
    guint i_rowstride;
    guint o_rowstride;
    GstBuffer *obuf;
    guint i;
    GstMapInfo imap, omap;

    if (s->info.type == GST_PNM_TYPE_PIXMAP) {
      o_rowstride = 3 * s->info.width;
      i_rowstride = GST_ROUND_UP_4 (o_rowstride);
    } else {
      o_rowstride = s->info.width;
      i_rowstride = GST_ROUND_UP_4 (o_rowstride);
    }

    obuf = gst_buffer_new_and_alloc (o_rowstride * s->info.height);
    gst_buffer_map (obuf, &omap, GST_MAP_WRITE);
    gst_buffer_map (buf, &imap, GST_MAP_READ);
    for (i = 0; i < s->info.height; i++)
      memcpy (omap.data + o_rowstride * i, imap.data + i_rowstride * i,
          o_rowstride);
    gst_buffer_unmap (buf, &imap);
    gst_buffer_unmap (obuf, &omap);
    gst_buffer_unref (buf);
    buf = obuf;
  } else {
    /* Pass through the data. */
    buf = gst_buffer_make_writable (buf);
  }

  /* We might need to convert to ASCII... */
  if (s->info.encoding == GST_PNM_ENCODING_ASCII) {
    GstBuffer *obuf;
    guint i, o;
    GstMapInfo imap, omap;

    gst_buffer_map (buf, &imap, GST_MAP_READ);
    obuf = gst_buffer_new_and_alloc (imap.size * (4 + 1 / 20.));
    gst_buffer_map (obuf, &omap, GST_MAP_WRITE);
    for (i = o = 0; i < imap.size; i++) {
      g_snprintf ((char *) omap.data + o, 4, "%3i", imap.data[i]);
      o += 3;
      omap.data[o++] = ' ';
      if (!((i + 1) % 20))
        omap.data[o++] = '\n';
    }
    gst_buffer_unmap (buf, &imap);
    gst_buffer_unmap (obuf, &omap);
    gst_buffer_unref (buf);
    buf = obuf;
  }

  r = gst_pad_push (s->src, buf);

out:

  return r;

not_negotiated:
  {
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_pnmenc_setcaps (GstPnmenc * s, GstCaps * caps)
{
  gboolean r;
  GstCaps *srccaps;

  s->info.max = 255;
  s->info.fields = GST_PNM_INFO_FIELDS_MAX;

  if (!gst_video_info_from_caps (&s->vinfo, caps))
    return FALSE;

  if (GST_VIDEO_INFO_IS_RGB (&s->vinfo)) {
    s->info.type = GST_PNM_TYPE_PIXMAP;
    srccaps = gst_caps_from_string (MIME_PM);
  } else if (GST_VIDEO_INFO_IS_GRAY (&s->vinfo)) {
    s->info.type = GST_PNM_TYPE_GRAYMAP;
    srccaps = gst_caps_from_string (MIME_GM);
  } else {
    return FALSE;
  }
  r = gst_pad_set_caps (s->src, srccaps);
  gst_caps_unref (srccaps);
  s->info.fields |= GST_PNM_INFO_FIELDS_TYPE;

  /* Remember width and height of the input data. */
  s->info.width = GST_VIDEO_INFO_WIDTH (&s->vinfo);
  s->info.height = GST_VIDEO_INFO_HEIGHT (&s->vinfo);
  s->info.fields |= GST_PNM_INFO_FIELDS_WIDTH | GST_PNM_INFO_FIELDS_HEIGHT;

  return r;
}

static gboolean
gst_pnmenc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstPnmenc *s = GST_PNMENC (parent);
  gboolean r = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      r = gst_pnmenc_setcaps (s, caps);
      gst_event_unref (event);
      break;
    }
    default:
      r = gst_pad_event_default (pad, parent, event);
      break;
  }

  return r;
}

static void
gst_pnmenc_init (GstPnmenc * s)
{
  GstPad *pad;

  pad = gst_pad_new_from_static_template (&sink_pad_template, "sink");
  gst_pad_set_chain_function (pad, gst_pnmenc_chain);
  gst_pad_set_event_function (pad, gst_pnmenc_sink_event);
  gst_pad_use_fixed_caps (pad);
  gst_element_add_pad (GST_ELEMENT (s), pad);

  s->src = gst_pad_new_from_static_template (&src_pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (s), s->src);
}

static void
gst_pnmenc_class_init (GstPnmencClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_pad_template));
  gst_element_class_set_static_metadata (element_class, "PNM image encoder",
      "Codec/Encoder/Image",
      "Encodes images into portable pixmap or graymap (PNM) format",
      "Lutz Mueller <lutz@users.sourceforge.net>");

  gobject_class->set_property = gst_pnmenc_set_property;
  gobject_class->get_property = gst_pnmenc_get_property;

  g_object_class_install_property (gobject_class, GST_PNMENC_PROP_ASCII,
      g_param_spec_boolean ("ascii", "ASCII Encoding", "The output will be "
          "ASCII encoded", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
