/* GStreamer
 *
 * gstv4lcolorbalance.c: color balance interface implementation for V4L
 *
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstv4lcolorbalance.h"
#include "gstv4lelement.h"

static void
gst_v4l_color_balance_channel_class_init (GstV4lColorBalanceChannelClass *
    klass);
static void gst_v4l_color_balance_channel_init (GstV4lColorBalanceChannel *
    channel);

static const GList *gst_v4l_color_balance_list_channels (GstColorBalance *
    balance);
static void gst_v4l_color_balance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value);
static gint gst_v4l_color_balance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel);

static GstColorBalanceChannelClass *parent_class = NULL;

GType
gst_v4l_color_balance_channel_get_type (void)
{
  static GType gst_v4l_color_balance_channel_type = 0;

  if (!gst_v4l_color_balance_channel_type) {
    static const GTypeInfo v4l_tuner_channel_info = {
      sizeof (GstV4lColorBalanceChannelClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_v4l_color_balance_channel_class_init,
      NULL,
      NULL,
      sizeof (GstV4lColorBalanceChannel),
      0,
      (GInstanceInitFunc) gst_v4l_color_balance_channel_init,
      NULL
    };

    gst_v4l_color_balance_channel_type =
        g_type_register_static (GST_TYPE_COLOR_BALANCE_CHANNEL,
        "GstV4lColorBalanceChannel", &v4l_tuner_channel_info, 0);
  }

  return gst_v4l_color_balance_channel_type;
}

static void
gst_v4l_color_balance_channel_class_init (GstV4lColorBalanceChannelClass *
    klass)
{
  parent_class = g_type_class_ref (GST_TYPE_COLOR_BALANCE_CHANNEL);
}

static void
gst_v4l_color_balance_channel_init (GstV4lColorBalanceChannel * channel)
{
  channel->index = 0;
}

void
gst_v4l_color_balance_interface_init (GstColorBalanceClass * klass)
{
  GST_COLOR_BALANCE_TYPE (klass) = GST_COLOR_BALANCE_HARDWARE;

  /* default virtual functions */
  klass->list_channels = gst_v4l_color_balance_list_channels;
  klass->set_value = gst_v4l_color_balance_set_value;
  klass->get_value = gst_v4l_color_balance_get_value;
}

static gboolean
gst_v4l_color_balance_contains_channel (GstV4lElement * v4lelement,
    GstV4lColorBalanceChannel * v4lchannel)
{
  const GList *item;

  for (item = v4lelement->colors; item != NULL; item = item->next)
    if (item->data == v4lchannel)
      return TRUE;

  return FALSE;
}

static const GList *
gst_v4l_color_balance_list_channels (GstColorBalance * balance)
{
  return GST_V4LELEMENT (balance)->colors;
}

static void
gst_v4l_color_balance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (balance);
  GstV4lColorBalanceChannel *v4lchannel =
      GST_V4L_COLOR_BALANCE_CHANNEL (channel);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L_IS_OPEN (v4lelement));
  g_return_if_fail (gst_v4l_color_balance_contains_channel (v4lelement,
          v4lchannel));

  gst_v4l_set_picture (v4lelement, v4lchannel->index, value);
}

static gint
gst_v4l_color_balance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (balance);
  GstV4lColorBalanceChannel *v4lchannel =
      GST_V4L_COLOR_BALANCE_CHANNEL (channel);
  gint value;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L_IS_OPEN (v4lelement), 0);
  g_return_val_if_fail (gst_v4l_color_balance_contains_channel (v4lelement,
          v4lchannel), 0);

  if (!gst_v4l_get_picture (v4lelement, v4lchannel->index, &value))
    return 0;

  return value;
}
