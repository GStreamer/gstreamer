/* GStreamer
  * Copyright (C) 2026 Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth266reorder.h"
#include <gst/codecparsers/gsth266parser.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_h266_reorder_debug);
#define GST_CAT_DEFAULT gst_h266_reorder_debug

struct _GstH266Reorder
{
  GstObject parent;

  GstH266Parser *parser;

  guint nal_length_size;
  gboolean is_vvc;

  GPtrArray *output_queue;
  guint32 system_num;

  GstClockTime latency;
};

static void gst_h266_reorder_finalize (GObject * object);

#define gst_h266_reorder_parent_class parent_class
G_DEFINE_TYPE (GstH266Reorder, gst_h266_reorder, GST_TYPE_OBJECT);

static void
gst_h266_reorder_class_init (GstH266ReorderClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_h266_reorder_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_h266_reorder_debug, "h266reorder", 0,
      "h266reorder");
}

static void
gst_h266_reorder_init (GstH266Reorder * self)
{
  self->parser = gst_h266_parser_new ();
  self->output_queue =
      g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_video_codec_frame_unref);
  self->nal_length_size = 4;
}

static void
gst_h266_reorder_finalize (GObject * object)
{
  GstH266Reorder *self = GST_H266_REORDER (object);

  gst_h266_parser_free (self->parser);
  g_ptr_array_unref (self->output_queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h266_reorder_parse_codec_data (GstH266Reorder * self, const guint8 * data,
    gsize size)
{
  GstH266Parser *parser = self->parser;
  GstH266ParserResult pres;
  GstH266DecoderConfigRecord *config = NULL;

  pres = gst_h266_parser_parse_decoder_config_record (parser,
      data, size, &config);
  if (pres != GST_H266_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse vvc1 data");
    return FALSE;
  }

  self->nal_length_size = config->length_size_minus_one + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", self->nal_length_size);

  gst_h266_decoder_config_record_free (config);
  return TRUE;
}

gboolean
gst_h266_reorder_set_caps (GstH266Reorder * self, GstCaps * caps,
    GstClockTime * latency)
{
  GstStructure *s;
  const gchar *str;
  const GValue *codec_data;
  gboolean ret = TRUE;
  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  self->is_vvc = FALSE;

  s = gst_caps_get_structure (caps, 0);
  str = gst_structure_get_string (s, "stream-format");
  if (g_strcmp0 (str, "vvc1") == 0 || g_strcmp0 (str, "vvi1") == 0) {
    self->is_vvc = TRUE;
  }

  codec_data = gst_structure_get_value (s, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    GstBuffer *buf = gst_value_get_buffer (codec_data);
    GstMapInfo info;
    if (gst_buffer_map (buf, &info, GST_MAP_READ)) {
      ret = gst_h266_reorder_parse_codec_data (self, info.data, info.size);
      gst_buffer_unmap (buf, &info);
    } else {
      GST_ERROR_OBJECT (self, "Couldn't map codec data");
      ret = FALSE;
    }
  }

  *latency = 0;
  return ret;
}

GstH266Reorder *
gst_h266_reorder_new (gboolean need_reorder)
{
  GstH266Reorder *self = g_object_new (GST_TYPE_H266_REORDER, NULL);
  gst_object_ref_sink (self);
  return self;
}

void
gst_h266_reorder_drain (GstH266Reorder * reorder)
{

}

gboolean
gst_h266_reorder_push (GstH266Reorder * reorder, GstVideoCodecFrame * frame,
    GstClockTime * latency)
{
  frame->system_frame_number = reorder->system_num;
  frame->decode_frame_number = reorder->system_num;

  GST_LOG_OBJECT (reorder,
      "Push frame %u, output queue size %u",
      frame->system_frame_number, reorder->output_queue->len);

  reorder->system_num++;

  g_ptr_array_add (reorder->output_queue, frame);
  *latency = 0;
  return TRUE;
}

GstVideoCodecFrame *
gst_h266_reorder_pop (GstH266Reorder * reorder)
{
  if (!reorder->output_queue->len) {
    GST_LOG_OBJECT (reorder, "Empty output queue");
    return NULL;
  }

  return g_ptr_array_steal_index (reorder->output_queue, 0);
}

guint
gst_h266_reorder_get_num_buffered (GstH266Reorder * reorder)
{
  return reorder->output_queue->len;
}

GstBuffer *
gst_h266_reorder_insert_sei (GstH266Reorder * reorder, GstBuffer * au,
    GArray * sei)
{
  GstBuffer *new_buf = NULL;
  GstBuffer *tmp_buf = NULL;
  guint i;

  if (!sei || sei->len == 0) {
    GST_WARNING_OBJECT (reorder, "Empty SEI array");
    return NULL;
  }

  new_buf = gst_buffer_ref (au);

  for (i = 0; i < sei->len; i++) {
    GstMemory *mem;
    GArray *single_sei;
    GstH266SEIMessage *sei_msg;

    /* Create an array with a single SEI message */
    single_sei = g_array_new (FALSE, FALSE, sizeof (GstH266SEIMessage));
    sei_msg = &g_array_index (sei, GstH266SEIMessage, i);
    g_array_append_val (single_sei, *sei_msg);

    if (reorder->is_vvc) {
      mem =
          gst_h266_create_sei_memory_vvc (0, 1, reorder->nal_length_size,
          single_sei, GST_H266_SEI_NAL_UNIT_TYPE_AUTO);
    } else {
      mem =
          gst_h266_create_sei_memory (0, 1, 4, single_sei,
          GST_H266_SEI_NAL_UNIT_TYPE_AUTO);
    }

    g_array_free (single_sei, TRUE);

    if (!mem) {
      GST_ERROR_OBJECT (reorder, "Couldn't create SEI memory for message %u",
          i);
      gst_buffer_unref (new_buf);
      return NULL;
    }

    if (reorder->is_vvc) {
      tmp_buf = gst_h266_parser_insert_sei_vvc (reorder->parser,
          reorder->nal_length_size, new_buf, mem);
    } else {
      tmp_buf = gst_h266_parser_insert_sei (reorder->parser, new_buf, mem);
    }

    gst_memory_unref (mem);

    if (!tmp_buf) {
      GST_ERROR_OBJECT (reorder, "Failed to insert SEI message %u", i);
      gst_buffer_unref (new_buf);
      return NULL;
    }

    gst_buffer_unref (new_buf);
    new_buf = tmp_buf;
  }

  return new_buf;
}
