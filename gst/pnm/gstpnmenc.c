/* GStreamer
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
 * SECTION:element-pnmenc
 *
 * Encodes pnm images.
 *
 * <refsect">
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc num_buffers=1 ! pnmenc ! ffmpegcolorspace ! "video/x-raw-gray" ! pnmenc ! filesink location=test.pnm
 * ]| The above pipeline writes a test pnm file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpnmenc.h"
#include "gstpnmutils.h"

#include <gst/gstutils.h>
#include <gst/video/video.h>

#include <string.h>

static GstElementDetails pnmenc_details =
GST_ELEMENT_DETAILS ("PNM converter", "Codec/Encoder/Image",
    "Encodes in PNM format",
    "Lutz Mueller <lutz@users.sourceforge.net>");

static GstStaticPadTemplate sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB "; "
        "video/x-raw-gray, width =" GST_VIDEO_SIZE_RANGE ", "
        "height =" GST_VIDEO_SIZE_RANGE ", framerate =" GST_VIDEO_FPS_RANGE ", "
        "bpp= (int) 8, depth= (int) 8"));

static GstStaticPadTemplate src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MIME_ALL));

static GstFlowReturn
gst_pnmenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstPnmenc *s = GST_PNMENC (gst_pad_get_parent (pad));
  GstFlowReturn r;
  gchar *header;
  GstBuffer *out;

  if (s->info.fields != GST_PNM_INFO_FIELDS_ALL) {
    r = GST_FLOW_NOT_NEGOTIATED;
    goto out;
  }

  /* Assumption: One buffer, one image. That is, always first write header. */
  header = g_strdup_printf ("P%i\n%i %i\n%i\n",
      s->info.type, s->info.width, s->info.height, s->info.max);
  out = gst_buffer_new ();
  gst_buffer_set_data (out, (guchar *) header, strlen (header));
  gst_buffer_set_caps (out, GST_PAD_CAPS (s->src));
  if ((r = gst_pad_push (s->src, out)) != GST_FLOW_OK)
    goto out;

  /* Need to convert from GStreamer rowstride to PNM rowstride */
  if (s->info.width % 4 != 0) {
    guint i_rowstride;
    guint o_rowstride;
    GstBuffer *obuf;
    guint i;

    if (s->info.type == GST_PNM_TYPE_PIXMAP_RAW) {
      o_rowstride = 3 * s->info.width;
      i_rowstride = GST_ROUND_UP_4 (o_rowstride);
    } else {
      o_rowstride = s->info.width;
      i_rowstride = GST_ROUND_UP_4 (o_rowstride);
    }

    obuf = gst_buffer_new_and_alloc (o_rowstride * s->info.height);
    for (i = 0; i < s->info.height; i++)
      memcpy (GST_BUFFER_DATA (obuf) + o_rowstride * i,
          GST_BUFFER_DATA (buf) + i_rowstride * i, o_rowstride);
    gst_buffer_unref (buf);
    buf = obuf;
  } else {
    /* Pass through the data. */
    buf = gst_buffer_make_metadata_writable (buf);
  }
  gst_buffer_set_caps (buf, GST_PAD_CAPS (s->src));
  r = gst_pad_push (s->src, buf);

out:
  gst_object_unref (s);

  return r;
}

static gboolean
gst_pnmenc_setcaps_func_sink (GstPad * pad, GstCaps * caps)
{
  GstPnmenc *s = GST_PNMENC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *mime = gst_structure_get_name (structure);
  gboolean r = TRUE;
  GstCaps *srccaps;

  s->info.max = 255;
  s->info.fields = GST_PNM_INFO_FIELDS_MAX;

  /* Set caps on the source. */
  if (!strcmp (mime, "video/x-raw-rgb")) {
    s->info.type = GST_PNM_TYPE_PIXMAP_RAW;
    srccaps = gst_caps_from_string (MIME_PM);
  } else if (!strcmp (mime, "video/x-raw-gray")) {
    s->info.type = GST_PNM_TYPE_GRAYMAP_RAW;
    srccaps = gst_caps_from_string (MIME_GM);
  } else {
    r = FALSE;
    goto out;
  }
  gst_pad_set_caps (s->src, srccaps);
  gst_caps_unref (srccaps);
  s->info.fields |= GST_PNM_INFO_FIELDS_TYPE;

  /* Remember width and height of the input data. */
  if (!gst_structure_get_int (structure, "width", (int *) &s->info.width) ||
      !gst_structure_get_int (structure, "height", (int *) &s->info.height)) {
    r = FALSE;
    goto out;
  }
  s->info.fields |= GST_PNM_INFO_FIELDS_WIDTH | GST_PNM_INFO_FIELDS_HEIGHT;

out:
  gst_object_unref (s);
  return r;
}

static void
gst_pnmenc_init (GstPnmenc * s, GstPnmencClass * klass)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_pad_template), "sink");
  gst_pad_set_setcaps_function (pad, gst_pnmenc_setcaps_func_sink);
  gst_pad_set_chain_function (pad, gst_pnmenc_chain);
  gst_pad_use_fixed_caps (pad);
  gst_element_add_pad (GST_ELEMENT (s), pad);

  s->src =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_pad_template), "src");
  gst_element_add_pad (GST_ELEMENT (s), s->src);
}

static void
gst_pnmenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_pad_template));
  gst_element_class_set_details (element_class, &pnmenc_details);
}

static void
gst_pnmenc_class_init (GstPnmencClass * klass)
{
  /* Nothing to see here. Move along. */
}

GST_BOILERPLATE (GstPnmenc, gst_pnmenc, GstElement, GST_TYPE_ELEMENT)
