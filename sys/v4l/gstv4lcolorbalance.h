/* G-Streamer generic V4L element - Color Balance interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4lcolorbalance.h: color balance interface implementation for V4L
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

#ifndef __GST_V4L_COLOR_BALANCE_H__
#define __GST_V4L_COLOR_BALANCE_H__

#include <gst/gst.h>
#include <gst/colorbalance/colorbalance.h>
#include "v4l_calls.h"

G_BEGIN_DECLS
#define GST_TYPE_V4L_COLOR_BALANCE_CHANNEL \
  (gst_v4l_color_balance_channel_get_type ())
#define GST_V4L_COLOR_BALANCE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L_COLOR_BALANCE_CHANNEL, \
			       GstV4lColorBalanceChannel))
#define GST_V4L_COLOR_BALANCE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_V4L_COLOR_BALANCE_CHANNEL, \
			    GstV4lColorBalanceChannelClass))
#define GST_IS_V4L_COLOR_BALANCE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L_COLOR_BALANCE_CHANNEL))
#define GST_IS_V4L_COLOR_BALANCE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_V4L_COLOR_BALANCE_CHANNEL))
    typedef struct _GstV4lColorBalanceChannel
{
  GstColorBalanceChannel parent;

  GstV4lPictureType index;
} GstV4lColorBalanceChannel;

typedef struct _GstV4lColorBalanceChannelClass
{
  GstColorBalanceChannelClass parent;
} GstV4lColorBalanceChannelClass;

GType gst_v4l_color_balance_channel_get_type (void);

void gst_v4l_color_balance_interface_init (GstColorBalanceClass * klass);

#endif /* __GST_V4L_COLOR_BALANCE_H__ */
