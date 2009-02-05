/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

/*
 * Includes
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstcamerabincolorbalance.h"
#include "gstcamerabin.h"

/*
 * static functions implementation
 */

static const GList *
gst_camerabin_color_balance_list_channels (GstColorBalance * cb)
{
  if (cb && GST_CAMERABIN (cb)->src_vid_src) {
    GstColorBalance *cbl = GST_COLOR_BALANCE (GST_CAMERABIN (cb)->src_vid_src);
    return gst_color_balance_list_channels (cbl);
  } else {
    return NULL;
  }
}

static void
gst_camerabin_color_balance_set_value (GstColorBalance * cb,
    GstColorBalanceChannel * channel, gint value)
{
  if (cb && GST_CAMERABIN (cb)->src_vid_src) {
    GstColorBalance *cbl = GST_COLOR_BALANCE (GST_CAMERABIN (cb)->src_vid_src);
    gst_color_balance_set_value (cbl, channel, value);
  }
}

static gint
gst_camerabin_color_balance_get_value (GstColorBalance * cb,
    GstColorBalanceChannel * channel)
{
  if (cb && GST_CAMERABIN (cb)->src_vid_src) {
    GstColorBalance *cbl = GST_COLOR_BALANCE (GST_CAMERABIN (cb)->src_vid_src);
    return gst_color_balance_get_value (cbl, channel);
  } else {
    return 0;
  }
}

/*
 * extern functions implementation
 */

void
gst_camerabin_color_balance_init (GstColorBalanceClass * iface)
{
  /* FIXME: to get the same type as v4l2src */
  GST_COLOR_BALANCE_TYPE (iface) = GST_COLOR_BALANCE_HARDWARE;
  iface->list_channels = gst_camerabin_color_balance_list_channels;
  iface->set_value = gst_camerabin_color_balance_set_value;
  iface->get_value = gst_camerabin_color_balance_get_value;
}
