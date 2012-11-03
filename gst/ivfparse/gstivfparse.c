/* -*- Mode: c; c-basic-offset: 2; indent-tabs-mode: nil -*- */
/*
 * GStreamer IVF parser
 * (c) 2010 Opera Software ASA, Philip Jägenstedt <philipj@opera.com>
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

/* Snippets of source code copied freely from wavparse,
 * aviparse and auparse. */

/* File format as written by libvpx ivfenc:
 *
 * All fields are little endian.
 *
 * 32 byte file header format:
 *
 * 0-3: "DKIF" (file magic)
 * 4-5: version (uint16)
 * 6-7: header size (uint16)
 * 8-11: "VP80" (FOURCC)
 * 12-13: width (uint16)
 * 14-15: height (uint16)
 * 16-19: framerate numerator (uint32)
 * 20-23: framerate denominator (uint32)
 * 24-27: frame count (uint32)
 * 28-31: unused
 *
 * 12 byte frame header format:
 *
 * 0-3: frame size in bytes (uint32)
 * 4-11: time stamp (uint64)
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstivfparse.h"

GST_DEBUG_CATEGORY_STATIC (gst_ivf_parse_debug);
#define GST_CAT_DEFAULT gst_ivf_parse_debug

/* sink and src pad templates */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ivf")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

GST_BOILERPLATE (GstIvfParse, gst_ivf_parse, GstElement, GST_TYPE_ELEMENT);

static void gst_ivf_parse_dispose (GObject * object);
static GstFlowReturn gst_ivf_parse_chain (GstPad * pad, GstBuffer * buf);

/* GObject vmethod implementations */

static void
gst_ivf_parse_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_static_metadata (element_class,
      "IVF parser",
      "Codec/Demuxer",
      "Demuxes a IVF stream", "Philip Jägenstedt <philipj@opera.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the ivfparse's class */
static void
gst_ivf_parse_class_init (GstIvfParseClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = gst_ivf_parse_dispose;
}

static void
gst_ivf_parse_reset (GstIvfParse * ivf)
{
  if (ivf->adapter) {
    gst_adapter_clear (ivf->adapter);
    g_object_unref (ivf->adapter);
    ivf->adapter = NULL;
  }
  ivf->state = GST_IVF_PARSE_START;
  ivf->rate_num = 0;
  ivf->rate_den = 0;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_ivf_parse_init (GstIvfParse * ivf, GstIvfParseClass * gclass)
{
  /* sink pad */
  ivf->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (ivf->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ivf_parse_chain));

  /* src pad */
  ivf->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (ivf->srcpad);

  gst_element_add_pad (GST_ELEMENT (ivf), ivf->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ivf), ivf->srcpad);

  /* reset */
  gst_ivf_parse_reset (ivf);
}

static void
gst_ivf_parse_dispose (GObject * object)
{
  GstIvfParse *ivf = GST_IVF_PARSE (object);

  GST_DEBUG_OBJECT (ivf, "disposing");
  gst_ivf_parse_reset (ivf);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* GstElement vmethod implementations */

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_ivf_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstIvfParse *ivf = GST_IVF_PARSE (GST_OBJECT_PARENT (pad));
  gboolean res;

  /* lazy creation of the adapter */
  if (G_UNLIKELY (ivf->adapter == NULL)) {
    ivf->adapter = gst_adapter_new ();
  }

  GST_LOG_OBJECT (ivf, "Pushing buffer of size %u to adapter",
      GST_BUFFER_SIZE (buf));

  gst_adapter_push (ivf->adapter, buf); /* adapter takes ownership of buf */

  res = GST_FLOW_OK;

  switch (ivf->state) {
    case GST_IVF_PARSE_START:
      if (gst_adapter_available (ivf->adapter) >= 32) {
        GstCaps *caps;

        const guint8 *data = gst_adapter_peek (ivf->adapter, 32);
        guint32 magic = GST_READ_UINT32_LE (data);
        guint16 version = GST_READ_UINT16_LE (data + 4);
        guint16 header_size = GST_READ_UINT16_LE (data + 6);
        guint32 fourcc = GST_READ_UINT32_LE (data + 8);
        guint16 width = GST_READ_UINT16_LE (data + 12);
        guint16 height = GST_READ_UINT16_LE (data + 14);
        guint32 rate_num = GST_READ_UINT32_LE (data + 16);
        guint32 rate_den = GST_READ_UINT32_LE (data + 20);
#ifndef GST_DISABLE_GST_DEBUG
        guint32 num_frames = GST_READ_UINT32_LE (data + 24);
#endif

        /* last 4 bytes unused */
        gst_adapter_flush (ivf->adapter, 32);

        if (magic != GST_MAKE_FOURCC ('D', 'K', 'I', 'F') ||
            version != 0 || header_size != 32 ||
            fourcc != GST_MAKE_FOURCC ('V', 'P', '8', '0')) {
          GST_ELEMENT_ERROR (ivf, STREAM, WRONG_TYPE, (NULL), (NULL));
          return GST_FLOW_ERROR;
        }

        /* create src pad caps */
        caps = gst_caps_new_simple ("video/x-vp8",
            "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
            "framerate", GST_TYPE_FRACTION, rate_num, rate_den, NULL);

        GST_INFO_OBJECT (ivf, "Found stream: %" GST_PTR_FORMAT, caps);

        GST_LOG_OBJECT (ivf, "Stream has %d frames", num_frames);

        gst_pad_set_caps (ivf->srcpad, caps);
        gst_caps_unref (caps);

        /* keep framerate in instance for convenience */
        ivf->rate_num = rate_num;
        ivf->rate_den = rate_den;

        gst_pad_push_event (ivf->srcpad, gst_event_new_new_segment (FALSE, 1.0,
                GST_FORMAT_TIME, 0, -1, 0));

        /* move along */
        ivf->state = GST_IVF_PARSE_DATA;
      } else {
        GST_LOG_OBJECT (ivf, "Header data not yet available.");
        break;
      }

      /* fall through */

    case GST_IVF_PARSE_DATA:
      while (gst_adapter_available (ivf->adapter) > 12) {
        const guint8 *data = gst_adapter_peek (ivf->adapter, 12);
        guint32 frame_size = GST_READ_UINT32_LE (data);
        guint64 frame_pts = GST_READ_UINT64_LE (data + 4);

        GST_LOG_OBJECT (ivf,
            "Read frame header: size %u, pts %" G_GUINT64_FORMAT, frame_size,
            frame_pts);

        if (gst_adapter_available (ivf->adapter) >= 12 + frame_size) {
          GstBuffer *frame;

          gst_adapter_flush (ivf->adapter, 12);

          frame = gst_adapter_take_buffer (ivf->adapter, frame_size);
          gst_buffer_set_caps (frame, GST_PAD_CAPS (ivf->srcpad));
          GST_BUFFER_TIMESTAMP (frame) =
              gst_util_uint64_scale_int (GST_SECOND * frame_pts, ivf->rate_den,
              ivf->rate_num);
          GST_BUFFER_DURATION (frame) =
              gst_util_uint64_scale_int (GST_SECOND, ivf->rate_den,
              ivf->rate_num);

          GST_DEBUG_OBJECT (ivf, "Pushing frame of size %u, ts %"
              GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", off %"
              G_GUINT64_FORMAT ", off_end %" G_GUINT64_FORMAT,
              GST_BUFFER_SIZE (frame),
              GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (frame)),
              GST_TIME_ARGS (GST_BUFFER_DURATION (frame)),
              GST_BUFFER_OFFSET (frame), GST_BUFFER_OFFSET_END (frame));

          res = gst_pad_push (ivf->srcpad, frame);
          if (res != GST_FLOW_OK)
            break;
        } else {
          GST_LOG_OBJECT (ivf, "Frame data not yet available.");
          break;
        }
      }
      break;

    default:
      g_return_val_if_reached (GST_FLOW_ERROR);
  }

  return res;
}

/* entry point to initialize the plug-in */
static gboolean
ivfparse_init (GstPlugin * ivfparse)
{
  /* debug category for filtering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_ivf_parse_debug, "ivfparse", 0, "IVF parser");

  /* register parser element */
  if (!gst_element_register (ivfparse, "ivfparse", GST_RANK_PRIMARY,
          GST_TYPE_IVF_PARSE))
    return FALSE;

  return TRUE;
}

/* gstreamer looks for this structure to register plugins */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ivfparse,
    "IVF parser",
    ivfparse_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
