/* GStreamer
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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

#ifndef __GST_BASE_PARSE_H__
#define __GST_BASE_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_PARSE		   (gst_base_parse_get_type())
#define GST_BASE_PARSE(obj)		   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_PARSE,GstBaseParse))
#define GST_BASE_PARSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_PARSE,GstBaseParseClass))
#define GST_BASE_PARSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_PARSE,GstBaseParseClass))
#define GST_IS_BASE_PARSE(obj)	   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_PARSE))
#define GST_IS_BASE_PARSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_PARSE))
#define GST_BASE_PARSE_CAST(obj)	((GstBaseParse *)(obj))

/**
 * GST_BASE_PARSE_SINK_NAME:
 *
 * the name of the templates for the sink pad
 */
#define GST_BASE_PARSE_SINK_NAME	"sink"
/**
 * GST_BASE_PARSE_SRC_NAME:
 *
 * the name of the templates for the source pad
 */
#define GST_BASE_PARSE_SRC_NAME	"src"

/**
 * GST_BASE_PARSE_SRC_PAD:
 * @obj: base parse instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 *
 * Since: 0.10.x
 */
#define GST_BASE_PARSE_SRC_PAD(obj)		(GST_BASE_PARSE_CAST (obj)->srcpad)

/**
 * GST_BASE_PARSE_SINK_PAD:
 * @obj: base parse instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 *
 * Since: 0.10.x
 */
#define GST_BASE_PARSE_SINK_PAD(obj)	(GST_BASE_PARSE_CAST (obj)->sinkpad)

/**
 * GST_BASE_PARSE_SEGMENT:
 * @obj: base parse instance
 *
 * Gives the segment of the element.
 *
 * Since: 0.10.x
 */
#define GST_BASE_PARSE_SEGMENT(obj)     (GST_BASE_PARSE_CAST (obj)->segment)

/**
 * GST_BASE_PARSE_FLOW_DROPPED:
 *
 * A #GstFlowReturn that can be returned from parse_frame to
 * indicate that no output buffer was generated, or from pre_push_buffer to
 * to forego pushing buffer.
 *
 * Since: 0.10.x
 */
#define GST_BASE_PARSE_FLOW_DROPPED   GST_FLOW_CUSTOM_SUCCESS

/**
 * GST_BASE_PARSE_FLOW_CLIP:
 *
 * A #GstFlowReturn that can be returned from pre_push_buffer to
 * indicate that regular segment clipping should be performed.
 *
 * Since: 0.10.x
 */
#define GST_BASE_PARSE_FLOW_CLIP      GST_FLOW_CUSTOM_SUCCESS_1

/**
 * GST_BASE_PARSE_BUFFER_FLAG_NO_FRAME:
 *
 * A #GstBufferFlag that can be set to have this buffer not counted as frame,
 * e.g. if this frame is dependent on a previous one.  As it is not counted as
 * a frame, bitrate increases but frame to time conversions are maintained.
 *
 * Since: 0.10.x
 */
#define GST_BASE_PARSE_BUFFER_FLAG_NO_FRAME     GST_BUFFER_FLAG_LAST


/**
 * GstBaseParseSeekable:
 * @GST_BASE_PARSE_SEEK_NONE: No seeking possible.
 * GST_BASE_PARSE_SEEK_DEFAULT: Default seeking possible using estimated bitrate.
 * GST_BASE_PARSE_SEEK_TABLE: Additional metadata provides more accurate seeking.
 *
 * Indicates what level (of quality) of seeking is possible.
 */
typedef enum _GstBaseParseSeekable {
  GST_BASE_PARSE_SEEK_NONE,
  GST_BASE_PARSE_SEEK_DEFAULT,
  GST_BASE_PARSE_SEEK_TABLE
} GstBaseParseSeekable;

typedef struct _GstBaseParse GstBaseParse;
typedef struct _GstBaseParseClass GstBaseParseClass;
typedef struct _GstBaseParsePrivate GstBaseParsePrivate;

/**
 * GstBaseParse:
 * @element: the parent element.
 *
 * The opaque #GstBaseParse data structure.
 */
struct _GstBaseParse {
  GstElement     element;
  GstAdapter    *adapter;

  /*< protected >*/
  /* source and sink pads */
  GstPad         *sinkpad;
  GstPad         *srcpad;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment      segment;

  /* Newsegment event to be sent after SEEK */
  GstEvent       *pending_segment;

  /* Segment event that closes the running segment prior to SEEK */
  GstEvent       *close_segment;

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
  GstBaseParsePrivate *priv;
};

/**
 * GstBaseParseClass:
 * @start:          Optional.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_sink_caps:  allows the subclass to be notified of the actual caps set.
 * @check_valid_frame:  Check if the given piece of data contains a valid
 *                      frame.
 * @parse_frame:    Parse the already checked frame. Subclass need to
 *                  set the buffer timestamp, duration, caps and possibly
 *                  other necessary metadata. This is called with srcpad's
 *                  STREAM_LOCK held.
 * @convert:        Optional.
 *                  Convert between formats.
 * @find_frame:     Optional.
 *                  Finds a frame. Gets a position passed and should return
 *                  TRUE and the offset in bytes where this position is.
 *                  Will only be called in pull mode and the subclass can pull
 *                  whatever it wants from upstream. If not implemented,
 *                  the base class will implement it by calling
 *                  @check_valid_frame and @parse_frame to find the wanted
 *                  frame and build a seek table.
 * @event:          Optional.
 *                  Event handler on the sink pad. This function should return
 *                  TRUE if the event was handled and can be dropped.
 * @src_event:      Optional.
 *                  Event handler on the source pad. Should return TRUE
 *                  if the event was handled and can be dropped.
 *
 * @get_frame_overhead: Finds the metadata overhead for the given frame. This
 *                      is used to enable more accurate bitrate computations.
 *                      If NULL, the per-frame overhead is assumed to be 0. If
 *                      this returns -1, it is assumed that this frame should
 *                      be skipped in bitrate calculation.
 *
 * @pre_push_buffer: Optional.
 *                   Called just prior to pushing a frame (after any pending
 *                   events have been sent) to give subclass a chance to perform
 *                   additional actions at this time (e.g. tag sending) or to
 *                   decide whether this buffer should be dropped or not
 *                   (e.g. custom segment clipping).
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @check_valid_frame and @parse_frame needs to be
 * overridden.
 */
struct _GstBaseParseClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean      (*start)              (GstBaseParse *parse);

  gboolean      (*stop)               (GstBaseParse *parse);

  gboolean      (*set_sink_caps)      (GstBaseParse *parse,
                                       GstCaps *caps);

  gboolean      (*check_valid_frame)  (GstBaseParse *parse,
                                       GstBuffer *buffer,
                                       guint *framesize,
                                       gint *skipsize);

  GstFlowReturn (*parse_frame)        (GstBaseParse *parse,
                                       GstBuffer *buffer);

  gboolean      (*convert)            (GstBaseParse * parse,
                                       GstFormat src_format,
                                       gint64 src_value,
                                       GstFormat dest_format,
                                       gint64 * dest_value);

  gboolean      (*event)              (GstBaseParse *parse,
                                       GstEvent *event);

  gboolean      (*src_event)          (GstBaseParse *parse,
                                       GstEvent *event);

  gint          (*get_frame_overhead) (GstBaseParse *parse,
                                       GstBuffer *buf);

  GstFlowReturn (*pre_push_buffer)    (GstBaseParse *parse,
                                       GstBuffer *buf);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];  
};

GType           gst_base_parse_get_type         (void);


GstFlowReturn gst_base_parse_push_buffer (GstBaseParse *parse,
                                          GstBuffer *buffer);

void gst_base_parse_set_duration (GstBaseParse *parse,
                                  GstFormat fmt, gint64 duration, gint interval);

void gst_base_parse_set_seek (GstBaseParse * parse,
                              GstBaseParseSeekable seek, guint bitrate);

void gst_base_parse_set_min_frame_size (GstBaseParse *parse,
                                        guint min_size);
void gst_base_parse_set_passthrough (GstBaseParse * parse, gboolean passthrough);

void gst_base_parse_set_frame_props (GstBaseParse * parse, guint fps_num,
                                     guint fps_den, guint lead_in, guint lead_out);

gboolean gst_base_parse_get_sync (GstBaseParse * parse);

gboolean gst_base_parse_get_drain (GstBaseParse * parse);

gboolean gst_base_parse_convert_default (GstBaseParse * parse,
                                         GstFormat src_format, gint64 src_value,
                                         GstFormat dest_format, gint64 * dest_value);

gboolean gst_base_parse_add_index_entry (GstBaseParse * parse, guint64 offset,
                                         GstClockTime ts, gboolean key, gboolean force);

G_END_DECLS

#endif /* __GST_BASE_PARSE_H__ */
