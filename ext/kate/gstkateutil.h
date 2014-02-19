/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) <2008> Vincent Penquerc'h <ogg.k.ogg.k at googlemail dot com>
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


#ifndef __GST_KATE_UTIL_H__
#define __GST_KATE_UTIL_H__

#include <kate/kate.h>
#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum {
  GST_KATE_FORMAT_UNDEFINED,
  GST_KATE_FORMAT_SPU,
  GST_KATE_FORMAT_TEXT_UTF8,
  GST_KATE_FORMAT_TEXT_PANGO_MARKUP
} GstKateFormat;

enum
{
  ARG_DEC_BASE_0,
  ARG_DEC_BASE_LANGUAGE,
  ARG_DEC_BASE_CATEGORY,
  ARG_DEC_BASE_ORIGINAL_CANVAS_WIDTH,
  ARG_DEC_BASE_ORIGINAL_CANVAS_HEIGHT,
  DECODER_BASE_ARG_COUNT
};

typedef struct
{
  GstEvent * event;
  gboolean (*handler)(GstPad *, GstObject*, GstEvent *);
  GstObject * parent;
  GstPad *pad;
} GstKateDecoderBaseQueuedEvent;

typedef struct
{
  GstElement element;

  kate_state k;

  gboolean initialized;

  GstTagList *tags;
  gboolean tags_changed;

  gchar *language;
  gchar *category;

  gint original_canvas_width;
  gint original_canvas_height;

  GstSegment kate_segment;
  gboolean kate_flushing;

  gboolean delay_events;
  GQueue *event_queue;
} GstKateDecoderBase;

extern GstCaps *gst_kate_util_set_header_on_caps (GstElement * element,
    GstCaps * caps, GList * headers);
extern void gst_kate_util_decode_base_init (GstKateDecoderBase * decoder,
    gboolean delay_events);
extern void gst_kate_util_install_decoder_base_properties (GObjectClass *
    gobject_class);
extern gboolean gst_kate_util_decoder_base_get_property (GstKateDecoderBase *
    decoder, GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec);
extern GstFlowReturn
gst_kate_util_decoder_base_chain_kate_packet (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstBuffer * buffer, GstPad * srcpad,
    GstPad * tagpad, GstCaps **src_caps, const kate_event ** ev);
extern void
gst_kate_util_decoder_base_set_flushing (GstKateDecoderBase * decoder,
    gboolean flushing);
extern void
gst_kate_util_decoder_base_segment_event (GstKateDecoderBase * decoder,
    GstEvent * event);
extern gboolean
gst_kate_util_decoder_base_update_segment (GstKateDecoderBase * decoder,
    GstElement * element, GstBuffer * buf);
extern GstStateChangeReturn
gst_kate_decoder_base_change_state (GstKateDecoderBase * decoder,
    GstElement * element, GstElementClass * parent_class,
    GstStateChange transition);
extern gboolean gst_kate_decoder_base_convert (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstFormat src_fmt, gint64 src_val,
    GstFormat * dest_fmt, gint64 * dest_val);
extern gboolean gst_kate_decoder_base_sink_query (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstObject * parent, GstQuery * query);
extern gboolean
gst_kate_util_decoder_base_queue_event (GstKateDecoderBase * decoder,
    GstEvent * event, gboolean (*handler)(GstPad *, GstObject *, GstEvent *),
    GstObject * parent, GstPad * pad);
extern void
gst_kate_util_decoder_base_add_tags (GstKateDecoderBase * decoder,
    GstTagList * tags, gboolean take_ownership_of_tags);
extern GstEvent *
gst_kate_util_decoder_base_get_tag_event (GstKateDecoderBase * decoder);
extern const char *
gst_kate_util_get_error_message (int ret);

G_END_DECLS
#endif /* __GST_KATE_UTIL_H__ */
