/* GStreamer Color Balance interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4l2colorbalance.c: color balance interface implementation for V4L2
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
#include "gstv4l2colorbalance.h"
#include "gstv4l2element.h"

static void
gst_v4l2_color_balance_channel_class_init (GstV4l2ColorBalanceChannelClass *
    klass);
static void gst_v4l2_color_balance_channel_init (GstV4l2ColorBalanceChannel *
    channel);

static const GList *gst_v4l2_color_balance_list_channels (GstColorBalance *
    balance);
static void gst_v4l2_color_balance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value);
static gint gst_v4l2_color_balance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel);

static GstColorBalanceChannelClass *parent_class = NULL;

GType
gst_v4l2_color_balance_channel_get_type (void)
{
  static GType gst_v4l2_color_balance_channel_type = 0;

  if (!gst_v4l2_color_balance_channel_type) {
    static const GTypeInfo v4l2_tuner_channel_info = {
      sizeof (GstV4l2ColorBalanceChannelClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_v4l2_color_balance_channel_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2ColorBalanceChannel),
      0,
      (GInstanceInitFunc) gst_v4l2_color_balance_channel_init,
      NULL
    };

    gst_v4l2_color_balance_channel_type =
	g_type_register_static (GST_TYPE_COLOR_BALANCE_CHANNEL,
	"GstV4l2ColorBalanceChannel", &v4l2_tuner_channel_info, 0);
  }

  return gst_v4l2_color_balance_channel_type;
}

static void
gst_v4l2_color_balance_channel_class_init (GstV4l2ColorBalanceChannelClass *
    klass)
{
  parent_class = g_type_class_ref (GST_TYPE_COLOR_BALANCE_CHANNEL);
}

static void
gst_v4l2_color_balance_channel_init (GstV4l2ColorBalanceChannel * channel)
{
  channel->index = 0;
}

void
gst_v4l2_color_balance_interface_init (GstColorBalanceClass * klass)
{
  GST_COLOR_BALANCE_TYPE (klass) = GST_COLOR_BALANCE_HARDWARE;

  /* default virtual functions */
  klass->list_channels = gst_v4l2_color_balance_list_channels;
  klass->set_value = gst_v4l2_color_balance_set_value;
  klass->get_value = gst_v4l2_color_balance_get_value;
}

static gboolean
gst_v4l2_color_balance_contains_channel (GstV4l2Element * v4l2element,
    GstV4l2ColorBalanceChannel * v4l2channel)
{
  const GList *item;

  for (item = v4l2element->colors; item != NULL; item = item->next)
    if (item->data == v4l2channel)
      return TRUE;

  return FALSE;
}

static const GList *
gst_v4l2_color_balance_list_channels (GstColorBalance * balance)
{
  return GST_V4L2ELEMENT (balance)->colors;
}

static void
gst_v4l2_color_balance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (balance);
  GstV4l2ColorBalanceChannel *v4l2channel =
      GST_V4L2_COLOR_BALANCE_CHANNEL (channel);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L2_IS_OPEN (v4l2element));
  g_return_if_fail (gst_v4l2_color_balance_contains_channel (v4l2element,
	  v4l2channel));

  gst_v4l2_set_attribute (v4l2element, v4l2channel->index, value);
}

static gint
gst_v4l2_color_balance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (balance);
  GstV4l2ColorBalanceChannel *v4l2channel =
      GST_V4L2_COLOR_BALANCE_CHANNEL (channel);
  gint value;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L2_IS_OPEN (v4l2element), 0);
  g_return_val_if_fail (gst_v4l2_color_balance_contains_channel (v4l2element,
	  v4l2channel), 0);

  if (!gst_v4l2_get_attribute (v4l2element, v4l2channel->index, &value))
    return 0;

  return value;
}
