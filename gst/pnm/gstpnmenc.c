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
 * gst-launch-1.0 videotestsrc num_buffers=1 ! videoconvert ! "video/x-raw,format=GRAY8" ! pnmenc ascii=true ! filesink location=test.pnm
 * ]| The above pipeline writes a test pnm file (ASCII encoding).
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpnmenc.h"
#include "gstpnmutils.h"

#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <stdio.h>

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

G_DEFINE_TYPE (GstPnmenc, gst_pnmenc, GST_TYPE_VIDEO_ENCODER);
#define parent_class gst_pnmenc_parent_class

static GstFlowReturn
gst_pnmenc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame);

static void gst_pnmenc_finalize (GObject * object);

static void
gst_pnmenc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstPnmenc *s = GST_PNMENC (object);

  switch (prop_id) {
    case GST_PNMENC_PROP_ASCII:
      if (g_value_get_boolean (value)) {
        s->info.encoding = GST_PNM_ENCODING_ASCII;
      } else {
        s->info.encoding = GST_PNM_ENCODING_RAW;
      }
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

static void
gst_pnmenc_init (GstPnmenc * s)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (s));

  /* Set default encoding as RAW as ASCII takes up 4 time more bytes */
  s->info.encoding = GST_PNM_ENCODING_RAW;
}

static void
gst_pnmenc_finalize (GObject * object)
{
  GstPnmenc *pnmenc = GST_PNMENC (object);
  if (pnmenc->input_state)
    gst_video_codec_state_unref (pnmenc->input_state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_pnmenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstPnmenc *pnmenc;
  gboolean ret = TRUE;
  GstVideoInfo *info;
  GstVideoCodecState *output_state;

  pnmenc = GST_PNMENC (encoder);
  info = &state->info;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_RGB:
      pnmenc->info.type = GST_PNM_TYPE_PIXMAP;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      pnmenc->info.type = GST_PNM_TYPE_GRAYMAP;
      break;
    default:
      ret = FALSE;
      goto done;
  }

  pnmenc->info.width = GST_VIDEO_INFO_WIDTH (info);
  pnmenc->info.height = GST_VIDEO_INFO_HEIGHT (info);
  /* Supported max value is only one, that is 255 */
  pnmenc->info.max = 255;

  if (pnmenc->input_state)
    gst_video_codec_state_unref (pnmenc->input_state);
  pnmenc->input_state = gst_video_codec_state_ref (state);

  output_state =
      gst_video_encoder_set_output_state (encoder,
      gst_caps_new_empty_simple ("image/pnm"), state);
  gst_video_codec_state_unref (output_state);

done:
  return ret;
}

static GstFlowReturn
gst_pnmenc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstPnmenc *pnmenc;
  guint size, pixels;
  GstMapInfo omap, imap;
  gchar *header = NULL;
  GstVideoInfo *info;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i_rowstride, o_rowstride;
  guint bytes = 0, index, head_size;
  guint i, j;

  pnmenc = GST_PNMENC (encoder);
  info = &pnmenc->input_state->info;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_RGB:
      pixels = size = pnmenc->info.width * pnmenc->info.height * 3;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      pixels = size = pnmenc->info.width * pnmenc->info.height * 1;
      break;
    default:
      ret = FALSE;
      goto done;
  }

  header = g_strdup_printf ("P%i\n%i %i\n%i\n",
      pnmenc->info.type + 3 * (1 - pnmenc->info.encoding), pnmenc->info.width,
      pnmenc->info.height, pnmenc->info.max);

  if (pnmenc->info.encoding == GST_PNM_ENCODING_ASCII) {
    /* Per component 4 bytes are used in case of ASCII encoding */
    size = size * 4 + size / 20;
    size += strlen (header);
    frame->output_buffer =
        gst_video_encoder_allocate_output_buffer (encoder, (size));
  } else {
    size += strlen (header);
    frame->output_buffer =
        gst_video_encoder_allocate_output_buffer (encoder, size);
  }

  if (gst_buffer_map (frame->output_buffer, &omap, GST_MAP_WRITE) == FALSE) {
    ret = GST_FLOW_ERROR;
    goto done;
  }
  if (gst_buffer_map (frame->input_buffer, &imap, GST_MAP_READ) == FALSE) {
    /* Unmap already mapped buffer */
    gst_buffer_unmap (frame->output_buffer, &omap);
    ret = GST_FLOW_ERROR;
    goto done;
  }
  memcpy (omap.data, header, strlen (header));

  head_size = strlen (header);
  if (pnmenc->info.encoding == GST_PNM_ENCODING_ASCII) {
    /* We need to convert to ASCII */
    if (pnmenc->info.width % 4 != 0) {
      /* Convert from gstreamer rowstride to PNM rowstride */
      if (pnmenc->info.type == GST_PNM_TYPE_PIXMAP) {
        o_rowstride = 3 * pnmenc->info.width;
      } else {
        o_rowstride = pnmenc->info.width;
      }
      i_rowstride = GST_VIDEO_FRAME_COMP_STRIDE (pnmenc->input_state, 0);

      for (i = 0; i < pnmenc->info.height; i++) {
        index = i * i_rowstride;
        for (j = 0; j < o_rowstride; j++, bytes++, index++) {
          g_snprintf ((char *) omap.data + head_size, 4, "%3i",
              imap.data[index]);
          head_size += 3;
          omap.data[head_size++] = ' ';
          /* Add new line so that file will not end up with sinle big line */
          if (!((bytes + 1) % 20))
            omap.data[head_size++] = '\n';
        }
      }
    } else {
      for (i = 0; i < pixels; i++) {
        g_snprintf ((char *) omap.data + head_size, 4, "%3i", imap.data[i]);
        head_size += 3;
        omap.data[head_size++] = ' ';
        if (!((i + 1) % 20))
          omap.data[head_size++] = '\n';
      }
    }
  } else {
    /* Need to convert from GStreamer rowstride to PNM rowstride */
    if (pnmenc->info.width % 4 != 0) {
      if (pnmenc->info.type == GST_PNM_TYPE_PIXMAP) {
        o_rowstride = 3 * pnmenc->info.width;
      } else {
        o_rowstride = pnmenc->info.width;
      }
      i_rowstride = GST_VIDEO_FRAME_COMP_STRIDE (pnmenc->input_state, 0);

      for (i = 0; i < pnmenc->info.height; i++)
        memcpy (omap.data + head_size + o_rowstride * i,
            imap.data + i_rowstride * i, o_rowstride);
    } else {
      /* size contains complete image size inlcuding header size,
         Exclude header size while copying data */
      memcpy (omap.data + strlen (header), imap.data, (size - head_size));
    }
  }

  gst_buffer_unmap (frame->output_buffer, &omap);
  gst_buffer_unmap (frame->input_buffer, &imap);

  if ((ret = gst_video_encoder_finish_frame (encoder, frame)) != GST_FLOW_OK)
    goto done;

done:
  g_free (header);
  return ret;
}

static void
gst_pnmenc_class_init (GstPnmencClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->set_property = gst_pnmenc_set_property;
  gobject_class->get_property = gst_pnmenc_get_property;

  g_object_class_install_property (gobject_class, GST_PNMENC_PROP_ASCII,
      g_param_spec_boolean ("ascii", "ASCII Encoding", "The output will be "
          "ASCII encoded", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_pad_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_pad_template));

  gst_element_class_set_static_metadata (element_class, "PNM image encoder",
      "Codec/Encoder/Image",
      "Encodes images into portable pixmap or graymap (PNM) format",
      "Lutz Mueller <lutz@users.sourceforge.net>");

  venc_class->set_format = gst_pnmenc_set_format;
  venc_class->handle_frame = gst_pnmenc_handle_frame;
  gobject_class->finalize = gst_pnmenc_finalize;
}
