/* Gnome-Streamer
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

#ifndef __GST_RTP_H263P_DEC_H__
#define __GST_RTP_H263P_DEC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H263P_DEC \
  (gst_rtph263pdec_get_type())
#define GST_RTP_H263P_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_H263P_DEC,GstRtpH263PDec))
#define GST_RTP_H263P_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_H263P_DEC,GstRtpH263PDec))
#define GST_IS_RTP_H263P_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_H263P_DEC))
#define GST_IS_RTP_H263P_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_H263P_DEC))

typedef struct _GstRtpH263PDec GstRtpH263PDec;
typedef struct _GstRtpH263PDecClass GstRtpH263PDecClass;

struct _GstRtpH263PDec
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstAdapter *adapter;

  guint frequency;
};

struct _GstRtpH263PDecClass
{
  GstElementClass parent_class;
};

gboolean gst_rtph263pdec_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_H263P_DEC_H__ */
