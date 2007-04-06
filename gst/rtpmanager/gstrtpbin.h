/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTP_BIN_H__
#define __GST_RTP_BIN_H__

#include <gst/gst.h>

#define GST_TYPE_RTP_BIN \
  (gst_rtp_bin_get_type())
#define GST_RTP_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_BIN,GstRTPBin))
#define GST_RTP_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_BIN,GstRTPBinClass))
#define GST_IS_RTP_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_BIN))
#define GST_IS_RTP_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_BIN))

typedef struct _GstRTPBin GstRTPBin;
typedef struct _GstRTPBinClass GstRTPBinClass;
typedef struct _GstRTPBinPrivate GstRTPBinPrivate;

struct _GstRTPBin {
  GstBin         bin;

  /* a list of session */
  GSList         *sessions;
  GstClock       *provided_clock;

  /*< private >*/
  GstRTPBinPrivate *priv;
};

struct _GstRTPBinClass {
  GstBinClass  parent_class;
};

GType gst_rtp_bin_get_type (void);

#endif /* __GST_RTP_BIN_H__ */
