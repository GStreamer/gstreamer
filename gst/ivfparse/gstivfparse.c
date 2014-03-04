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

#define IVF_FILE_HEADER_SIZE 32
#define IVF_FRAME_HEADER_SIZE 12

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

#define gst_ivf_parse_parent_class parent_class
G_DEFINE_TYPE (GstIvfParse, gst_ivf_parse, GST_TYPE_BASE_PARSE);

static void gst_ivf_parse_finalize (GObject * object);
static gboolean gst_ivf_parse_start (GstBaseParse * parse);
static gboolean gst_ivf_parse_stop (GstBaseParse * parse);

static GstFlowReturn
gst_ivf_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);

/* initialize the ivfparse's class */
static void
gst_ivf_parse_class_init (GstIvfParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseParseClass *gstbaseparse_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbaseparse_class = (GstBaseParseClass *) klass;

  gobject_class->finalize = gst_ivf_parse_finalize;

  gstbaseparse_class->start = gst_ivf_parse_start;
  gstbaseparse_class->stop = gst_ivf_parse_stop;
  gstbaseparse_class->handle_frame = gst_ivf_parse_handle_frame;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "IVF parser", "Codec/Demuxer",
      "Demuxes a IVF stream", "Philip Jägenstedt <philipj@opera.com>");

  /* debug category for filtering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_ivf_parse_debug, "ivfparse", 0, "IVF parser");
}

static void
gst_ivf_parse_reset (GstIvfParse * ivf)
{
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
gst_ivf_parse_init (GstIvfParse * ivf)
{
  gst_ivf_parse_reset (ivf);
}

static void
gst_ivf_parse_finalize (GObject * object)
{
  GstIvfParse *const ivf = GST_IVF_PARSE (object);

  GST_DEBUG_OBJECT (ivf, "finalizing");
  gst_ivf_parse_reset (ivf);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ivf_parse_start (GstBaseParse * parse)
{
  GstIvfParse *const ivf = GST_IVF_PARSE (parse);

  gst_ivf_parse_reset (ivf);

  /* Minimal file header size needed at start */
  gst_base_parse_set_min_frame_size (parse, IVF_FILE_HEADER_SIZE);

  /* No sync code to detect frame boundaries */
  gst_base_parse_set_syncable (parse, FALSE);

  return TRUE;
}

static gboolean
gst_ivf_parse_stop (GstBaseParse * parse)
{
  GstIvfParse *const ivf = GST_IVF_PARSE (parse);

  gst_ivf_parse_reset (ivf);

  return TRUE;
}

static GstFlowReturn
gst_ivf_parse_handle_frame_start (GstIvfParse * ivf, GstBaseParseFrame * frame,
    gint * skipsize)
{
  GstBuffer *const buffer = frame->buffer;
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  if (map.size >= IVF_FILE_HEADER_SIZE) {
    guint32 magic = GST_READ_UINT32_LE (map.data);
    guint16 version = GST_READ_UINT16_LE (map.data + 4);
    guint16 header_size = GST_READ_UINT16_LE (map.data + 6);
    guint32 fourcc = GST_READ_UINT32_LE (map.data + 8);
    guint16 width = GST_READ_UINT16_LE (map.data + 12);
    guint16 height = GST_READ_UINT16_LE (map.data + 14);
    guint32 rate_num = GST_READ_UINT32_LE (map.data + 16);
    guint32 rate_den = GST_READ_UINT32_LE (map.data + 20);
#ifndef GST_DISABLE_GST_DEBUG
    guint32 num_frames = GST_READ_UINT32_LE (map.data + 24);
#endif

    if (magic != GST_MAKE_FOURCC ('D', 'K', 'I', 'F') ||
        version != 0 || header_size != 32 ||
        fourcc != GST_MAKE_FOURCC ('V', 'P', '8', '0')) {
      GST_ELEMENT_ERROR (ivf, STREAM, WRONG_TYPE, (NULL), (NULL));
      ret = GST_FLOW_ERROR;
      goto end;
    }

    /* create src pad caps */
    caps = gst_caps_new_simple ("video/x-vp8",
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, rate_num, rate_den, NULL);

    GST_INFO_OBJECT (ivf, "Found stream: %" GST_PTR_FORMAT, caps);

    GST_LOG_OBJECT (ivf, "Stream has %d frames", num_frames);

    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (ivf), caps);
    gst_caps_unref (caps);

    /* keep framerate in instance for convenience */
    ivf->rate_num = rate_num;
    ivf->rate_den = rate_den;

    /* move along */
    ivf->state = GST_IVF_PARSE_DATA;
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE_CAST (ivf),
        IVF_FRAME_HEADER_SIZE);
    *skipsize = IVF_FILE_HEADER_SIZE;
  } else {
    GST_LOG_OBJECT (ivf, "Header data not yet available.");
    *skipsize = 0;
  }

end:
  gst_buffer_unmap (buffer, &map);
  return ret;
}

static GstFlowReturn
gst_ivf_parse_handle_frame_data (GstIvfParse * ivf, GstBaseParseFrame * frame,
    gint * skipsize)
{
  GstBuffer *const buffer = frame->buffer;
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buffer;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  if (map.size >= IVF_FILE_HEADER_SIZE) {
    guint32 frame_size = GST_READ_UINT32_LE (map.data);
    guint64 frame_pts = GST_READ_UINT64_LE (map.data + 4);

    GST_LOG_OBJECT (ivf,
        "Read frame header: size %u, pts %" G_GUINT64_FORMAT, frame_size,
        frame_pts);

    if (map.size < IVF_FRAME_HEADER_SIZE + frame_size) {
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE_CAST (ivf),
          IVF_FRAME_HEADER_SIZE + frame_size);
      gst_buffer_unmap (buffer, &map);
      *skipsize = 0;
      goto end;
    }

    gst_buffer_unmap (buffer, &map);

    /* Eventually, we would need the buffer memory in a merged state anyway */
    out_buffer = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_FLAGS |
        GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META |
        GST_BUFFER_COPY_MEMORY | GST_BUFFER_COPY_MERGE,
        IVF_FRAME_HEADER_SIZE, frame_size);
    if (!out_buffer) {
      GST_ERROR_OBJECT (ivf, "Failed to copy frame buffer");
      ret = GST_FLOW_ERROR;
      *skipsize = IVF_FRAME_HEADER_SIZE + frame_size;
      goto end;
    }
    gst_buffer_replace (&frame->out_buffer, out_buffer);
    gst_buffer_unref (out_buffer);

    GST_BUFFER_TIMESTAMP (out_buffer) =
        gst_util_uint64_scale_int (GST_SECOND * frame_pts, ivf->rate_den,
        ivf->rate_num);
    GST_BUFFER_DURATION (out_buffer) =
        gst_util_uint64_scale_int (GST_SECOND, ivf->rate_den, ivf->rate_num);

    GST_DEBUG_OBJECT (ivf, "Pushing frame of size %u, ts %"
        GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", off %"
        G_GUINT64_FORMAT ", off_end %" G_GUINT64_FORMAT,
        frame_size,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out_buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (out_buffer)),
        GST_BUFFER_OFFSET (out_buffer), GST_BUFFER_OFFSET_END (out_buffer));

    ret = gst_base_parse_finish_frame (GST_BASE_PARSE_CAST (ivf), frame,
        IVF_FRAME_HEADER_SIZE + frame_size);
    *skipsize = 0;
  } else {
    GST_LOG_OBJECT (ivf, "Frame data not yet available.");
    gst_buffer_unmap (buffer, &map);
    *skipsize = 0;
  }

end:
  return ret;
}

static GstFlowReturn
gst_ivf_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstIvfParse *const ivf = GST_IVF_PARSE (parse);

  switch (ivf->state) {
    case GST_IVF_PARSE_START:
      return gst_ivf_parse_handle_frame_start (ivf, frame, skipsize);
    case GST_IVF_PARSE_DATA:
      return gst_ivf_parse_handle_frame_data (ivf, frame, skipsize);
    default:
      break;
  }

  g_assert_not_reached ();
  return GST_FLOW_ERROR;
}

/* entry point to initialize the plug-in */
static gboolean
ivfparse_init (GstPlugin * ivfparse)
{
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
