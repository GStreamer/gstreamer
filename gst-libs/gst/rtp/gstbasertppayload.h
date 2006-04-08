/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_BASE_RTP_PAYLOAD_H__
#define __GST_BASE_RTP_PAYLOAD_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_RTP_PAYLOAD \
        (gst_basertppayload_get_type())
#define GST_BASE_RTP_PAYLOAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_RTP_PAYLOAD,GstBaseRTPPayload))
#define GST_BASE_RTP_PAYLOAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_RTP_PAYLOAD,GstBaseRTPPayloadClass))
#define GST_BASE_RTP_PAYLOAD_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASE_RTP_PAYLOAD, GstBaseRTPPayloadClass))
#define GST_IS_BASE_RTP_PAYLOAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_RTP_PAYLOAD))
#define GST_IS_BASE_RTP_PAYLOAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_RTP_PAYLOAD))

typedef struct _GstBaseRTPPayload GstBaseRTPPayload;
typedef struct _GstBaseRTPPayloadClass GstBaseRTPPayloadClass;

#define GST_BASE_RTP_PAYLOAD_SINKPAD(payload) (GST_BASE_RTP_PAYLOAD (payload)->sinkpad)
#define GST_BASE_RTP_PAYLOAD_SRCPAD(payload)  (GST_BASE_RTP_PAYLOAD (payload)->srcpad)

#define GST_BASE_RTP_PAYLOAD_PT(payload)  (GST_BASE_RTP_PAYLOAD (payload)->pt)
#define GST_BASE_RTP_PAYLOAD_MTU(payload) (GST_BASE_RTP_PAYLOAD (payload)->mtu)

struct _GstBaseRTPPayload
{
  GstElement element;

  /*< private >*/
  GstPad  *sinkpad;
  GstPad  *srcpad;

  GRand   *seq_rand;
  GRand   *ssrc_rand;
  GRand   *ts_rand;

  guint32  ts_base;
  guint16  seqnum_base;

  gchar   *media;
  gchar   *encoding_name;
  gboolean dynamic;
  guint32  clock_rate;

  gint32   ts_offset;
  guint32  timestamp;
  gint16   seqnum_offset;
  guint16  seqnum;
  gint64   max_ptime;
  guint    pt;
  guint    ssrc;
  guint    current_ssrc;
  guint    mtu;

  GstSegment segment;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBaseRTPPayloadClass
{
  GstElementClass parent_class;

  /* receive caps on the sink pad, configure the payloader. */
  gboolean      (*set_caps)             (GstBaseRTPPayload *payload, GstCaps *caps);
  /* handle a buffer, perform 0 or more gst_basertppayload_push() on
   * the RTP buffers */
  GstFlowReturn (*handle_buffer)        (GstBaseRTPPayload *payload, 
                                         GstBuffer *buffer);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType           gst_basertppayload_get_type             (void);

void            gst_basertppayload_set_options          (GstBaseRTPPayload *payload, 
                                                         gchar *media, gboolean dynamic,
                                                         gchar *encoding_name,
                                                         guint32 clock_rate);

gboolean        gst_basertppayload_set_outcaps          (GstBaseRTPPayload *payload, 
                                                         gchar *fieldname, ...);

gboolean        gst_basertppayload_is_filled            (GstBaseRTPPayload *payload,
                                                         guint size, GstClockTime duration);

GstFlowReturn   gst_basertppayload_push                 (GstBaseRTPPayload *payload, 
                                                         GstBuffer *buffer);

G_END_DECLS

#endif /* __GST_BASE_RTP_PAYLOAD_H__ */
