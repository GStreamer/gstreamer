/* GStreamer Color Balance
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * colorbalance.c: image color balance interface design
 *		   virtual class function wrappers
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

#include "colorbalance.h"

static void 	gst_color_balance_class_init	(GstColorBalanceClass *klass);

GType
gst_color_balance_get_type (void)
{
  static GType gst_color_balance_type = 0;

  if (!gst_color_balance_type) {
    static const GTypeInfo gst_color_balance_info = {
      sizeof (GstColorBalanceClass),
      (GBaseInitFunc) gst_color_balance_class_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_color_balance_type = g_type_register_static (G_TYPE_INTERFACE,
						     "GstColorBalance",
						     &gst_color_balance_info, 0);
    g_type_interface_add_prerequisite (gst_color_balance_type,
				       GST_TYPE_INTERFACE);
  }

  return gst_color_balance_type;
}

static void
gst_color_balance_class_init (GstColorBalanceClass *klass)
{
  /* default virtual functions */
  klass->list_channels = NULL;
  klass->set_value = NULL;
  klass->get_value = NULL;
}

const GList *
gst_color_balance_list_channels	(GstColorBalance *balance)
{
  GstColorBalanceClass *klass = GST_COLOR_BALANCE_GET_CLASS (balance);

  if (klass->list_channels) {
    return klass->list_channels (balance);
  }

  return NULL;
}

void
gst_color_balance_set_value (GstColorBalance        *balance,
			     GstColorBalanceChannel *channel,
			     gint                    value)
{
  GstColorBalanceClass *klass = GST_COLOR_BALANCE_GET_CLASS (balance);

  if (klass->set_value) {
    klass->set_value (balance, channel, value);
  }
}

gint
gst_color_balance_get_value (GstColorBalance        *balance,
			     GstColorBalanceChannel *channel)
{
  GstColorBalanceClass *klass = GST_COLOR_BALANCE_GET_CLASS (balance);

  if (klass->get_value) {
    return klass->get_value (balance, channel);
  }

  return channel->min_value;
}
