/* GStreamer
 * Copyright (C) <2025> Jan Schmidt <jan@centricular.com>
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
 * SECTION:element-id3metaparse
 * @title: id3metaparse
 *
 * This element collects timed ID3 metadata packets into complete ID3 frames
 * and extracts the tags
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -vt filesrc location=mpegts-with-id3-track.ts ! tsdemux ! id3metaparse ! fakesink silent=false
 * ]| Extract and display the contents of a timed ID3 metadata track
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>
#include <gst/tag/tag.h>

#include "gstid3metaparse.h"

GST_DEBUG_CATEGORY_STATIC (id3metaparse_debug);
#define GST_CAT_DEFAULT id3metaparse_debug

static GstStaticPadTemplate id3meta_parse_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("meta/x-id3, parsed = (boolean) true")
    );

static GstStaticPadTemplate id3meta_parse_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("meta/x-id3")
    );

static gboolean gst_id3meta_parse_start (GstBaseParse * parse);
static GstFlowReturn gst_id3meta_parse_handle_frame (GstBaseParse * base,
    GstBaseParseFrame * frame, gint * skip);
static gboolean id3metaparse_element_init (GstPlugin * plugin);

G_DEFINE_TYPE (GstId3MetaParse, gst_id3meta_parse, GST_TYPE_BASE_PARSE);
GST_ELEMENT_REGISTER_DEFINE_CUSTOM (id3metaparse, id3metaparse_element_init);

static void
gst_id3meta_parse_class_init (GstId3MetaParseClass * klass)
{
  GstBaseParseClass *bpclass = (GstBaseParseClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  bpclass->start = GST_DEBUG_FUNCPTR (gst_id3meta_parse_start);
  bpclass->handle_frame = GST_DEBUG_FUNCPTR (gst_id3meta_parse_handle_frame);

  gst_element_class_add_static_pad_template (element_class,
      &id3meta_parse_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &id3meta_parse_sink_factory);
  gst_element_class_set_static_metadata (element_class,
      "ID3 Timed Metadata parser", "Codec/Parser/Meta",
      "Collects timed ID3 metadata streams into complete packets",
      "Jan Schmidt <jan@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (id3metaparse_debug, "id3metaparse", 0,
      "Timed ID3 metadata parsing element");
}

static void
gst_id3meta_parse_init (GstId3MetaParse * parse)
{
}

static gboolean
gst_id3meta_parse_start (GstBaseParse * base)
{
  gst_base_parse_set_min_frame_size (base, GST_TAG_ID3V2_HEADER_SIZE);
  return TRUE;
}

static GstFlowReturn
gst_id3meta_parse_handle_frame (GstBaseParse * base,
    GstBaseParseFrame * frame, gint * skip)
{
  GstId3MetaParse *parse = GST_ID3_META_PARSE (base);
  gsize buf_size;

  GST_DEBUG_OBJECT (parse, "Checking for frame in buffer %" GST_PTR_FORMAT,
      frame->buffer);

  buf_size = gst_buffer_get_size (frame->buffer);

  if (GST_BUFFER_FLAG_IS_SET (frame->buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
    GST_DEBUG_OBJECT (parse, "Skipping delta buffer");
    *skip = buf_size;
    return GST_FLOW_OK;
  }

  g_assert (buf_size >= GST_TAG_ID3V2_HEADER_SIZE);

  guint tag_size = gst_tag_get_id3v2_tag_size (frame->buffer);
  if (buf_size < tag_size) {
    GST_DEBUG_OBJECT (parse,
        "ID3 tag is %u bytes. Currently have %" G_GSIZE_FORMAT
        " bytes - waiting for more", tag_size, buf_size);
    return GST_FLOW_OK;
  }

  /* Accumulated enough data for this tag now */
  GST_DEBUG_OBJECT (parse,
      "Got ID3 metadata packet of size %u bytes", tag_size);
  GstTagList *tags = gst_tag_list_from_id3v2_tag (frame->buffer);
  if (tags != NULL) {
    gst_base_parse_merge_tags (base, tags, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (tags);
  }

  GstFlowReturn ret = gst_base_parse_finish_frame (base, frame, tag_size);
  return ret;
}

static gboolean
id3metaparse_element_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "id3metaparse", GST_RANK_PRIMARY,
          GST_TYPE_ID3_META_PARSE))
    return FALSE;

  return TRUE;
}
