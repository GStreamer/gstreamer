/* G-Streamer generic V4L2 element - Color Balance interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4l2colorbalance.h: color balance interface implementation for V4L2
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

#ifndef __GST_V4L2_COLOR_BALANCE_H__
#define __GST_V4L2_COLOR_BALANCE_H__

#include <gst/gst.h>
#include <gst/colorbalance/colorbalance.h>
#include "v4l2_calls.h"

G_BEGIN_DECLS
#define GST_TYPE_V4L2_COLOR_BALANCE_CHANNEL \
  (gst_v4l2_color_balance_channel_get_type ())
#define GST_V4L2_COLOR_BALANCE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_COLOR_BALANCE_CHANNEL, \
			       GstV4l2ColorBalanceChannel))
#define GST_V4L2_COLOR_BALANCE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_V4L2_COLOR_BALANCE_CHANNEL, \
			    GstV4l2ColorBalanceChannelClass))
#define GST_IS_V4L2_COLOR_BALANCE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_COLOR_BALANCE_CHANNEL))
#define GST_IS_V4L2_COLOR_BALANCE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_V4L2_COLOR_BALANCE_CHANNEL))
    typedef struct _GstV4l2ColorBalanceChannel
{
  GstColorBalanceChannel parent;

  guint32 index;
} GstV4l2ColorBalanceChannel;

typedef struct _GstV4l2ColorBalanceChannelClass
{
  GstColorBalanceChannelClass parent;
} GstV4l2ColorBalanceChannelClass;

GType gst_v4l2_color_balance_channel_get_type (void);

void gst_v4l2_color_balance_interface_init (GstColorBalanceClass * klass);

#endif /* __GST_V4L2_COLOR_BALANCE_H__ */
