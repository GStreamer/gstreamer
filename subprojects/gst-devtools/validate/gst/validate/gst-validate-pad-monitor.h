/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-pad-monitor.h - Validate PadMonitor class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __GST_VALIDATE_PAD_MONITOR_H__
#define __GST_VALIDATE_PAD_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>

typedef struct _GstValidatePadMonitor GstValidatePadMonitor;
typedef struct _GstValidatePadMonitorClass GstValidatePadMonitorClass;
typedef struct _GstValidatePadSeekData GstValidatePadSeekData;

#include <gst/validate/gst-validate-monitor.h>
#include <gst/validate/media-descriptor-parser.h>
#include <gst/validate/gst-validate-element-monitor.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_PAD_MONITOR			(gst_validate_pad_monitor_get_type ())
#define GST_IS_VALIDATE_PAD_MONITOR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_PAD_MONITOR))
#define GST_IS_VALIDATE_PAD_MONITOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_PAD_MONITOR))
#define GST_VALIDATE_PAD_MONITOR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_PAD_MONITOR, GstValidatePadMonitorClass))
#define GST_VALIDATE_PAD_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_PAD_MONITOR, GstValidatePadMonitor))
#define GST_VALIDATE_PAD_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_PAD_MONITOR, GstValidatePadMonitorClass))
#define GST_VALIDATE_PAD_MONITOR_CAST(obj)            ((GstValidatePadMonitor*)(obj))
#define GST_VALIDATE_PAD_MONITOR_CLASS_CAST(klass)    ((GstValidatePadMonitorClass*)(klass))
#endif

/**
 * GstValidatePadMonitor:
 *
 * GStreamer Validate PadMonitor class.
 *
 * Class that wraps a #GstPad for Validate checks
 */
struct _GstValidatePadMonitor {
  GstValidateMonitor 	 parent;

  gboolean       setup;

  GstPadChainFunction chain_func;
  GstPadEventFunction event_func;
  GstPadEventFullFunction event_full_func;
  GstPadQueryFunction query_func;
  GstPadActivateModeFunction activatemode_func;
  GstPadGetRangeFunction get_range_func;

  gulong pad_probe_id;

  /*< private >*/
  /* Last caps pushed/received */
  GstCaps *last_caps;
  gboolean caps_is_audio;
  gboolean caps_is_video;
  gboolean caps_is_raw;

  /* FIXME : Let's migrate all those booleans into a 32 (or 64) bit flag */
  gboolean first_buffer;

  gboolean has_segment;
  gboolean is_eos;

  gboolean pending_flush_stop;
  guint32 pending_newsegment_seqnum;
  guint32 pending_eos_seqnum;

  /* List of GstValidatePadSeekData containing pending/current seeks */
  GList *seeks;
  GstValidatePadSeekData *current_seek;

  /* Whether the next buffer should have a DISCONT flag on it, because
   * it's the first one, or follows a SEGMENT and/or a FLUSH */
  gboolean pending_buffer_discont;

  GstEvent *expected_segment;
  GPtrArray *serialized_events;
  GList *expired_events;

  GstStructure *pending_setcaps_fields;

  GstCaps * last_refused_caps;
  GstCaps * last_query_filter;
  GstCaps * last_query_res;

  /* tracked data */
  GstSegment segment;
  GstClockTime current_timestamp;
  GstClockTime current_duration;

  /* Stores the timestamp range of data that has flown through
   * this pad by using TIMESTAMP and TIMESTAMP+DURATION from
   * incomming buffers. Every time a buffer is pushed, this range
   * is extended.
   *
   * When a buffer is pushed, the timestamp range is checked against
   * the outgoing timestamp to check it is in the received boundaries.
   */
  GstClockTime timestamp_range_start;
  GstClockTime timestamp_range_end;

  /* GstValidateMediaCheck related fields */
  GList *all_bufs;
  /* The GstBuffer that should arrive next in a GList */
  GList *current_buf;
  gboolean check_buffers;

  /* 'min-buffer-frequency' config check */
  gdouble min_buf_freq;
  gint buffers_pushed;
  gint last_buffers_pushed;
  GstClockTime min_buf_freq_interval_ts;
  GstClockTime min_buf_freq_first_buffer_ts;
  GstClockTime min_buf_freq_start;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstValidatePadMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate PadMonitor object class.
 */
struct _GstValidatePadMonitorClass {
  GstValidateMonitorClass	parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* normal GObject stuff */
GST_VALIDATE_API
GType		gst_validate_pad_monitor_get_type		(void);

GST_VALIDATE_API
GstValidatePadMonitor *   gst_validate_pad_monitor_new      (GstPad * pad, GstValidateRunner * runner, GstValidateElementMonitor * parent);

G_END_DECLS

#endif /* __GST_VALIDATE_PAD_MONITOR_H__ */

