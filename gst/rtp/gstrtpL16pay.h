/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_RTP_L16_PAY_H__
#define __GST_RTP_L16_PAY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* Definition of structure storing data for this element. */
typedef struct _GstRtpL16Pay GstRtpL16Pay;
struct _GstRtpL16Pay
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  guint frequency;
  guint channels;

  /* the timestamp of the next frame */
  guint64 next_time;
  /* the interval between frames */
  guint64 time_interval;
  
  guint32 ssrc;
  guint16 seq;
};

/* Standard definition defining a class for this element. */
typedef struct _GstRtpL16PayClass GstRtpL16PayClass;
struct _GstRtpL16PayClass
{
  GstElementClass parent_class;
};

/* Standard macros for defining types for this element.  */
#define GST_TYPE_RTP_L16_PAY \
  (gst_rtpL16pay_get_type())
#define GST_RTP_L16_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_L16_PAY,GstRtpL16Pay))
#define GST_RTP_L16_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_L16_PAY,GstRtpL16Pay))
#define GST_IS_RTP_L16_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_L16_PAY))
#define GST_IS_RTP_L16_PAY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_L16_PAY))

gboolean gst_rtpL16pay_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif				/* __GST_RTP_L16_PAY_H__ */
