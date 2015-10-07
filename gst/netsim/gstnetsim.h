/*
 * Farsight Voice+Video library
 *
 *  Copyright 2006 Collabora Ltd,
 *  Copyright 2006 Nokia Corporation
 *   @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>.
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
 *
 */

#ifndef __GST_NET_SIM_H__
#define __GST_NET_SIM_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_NET_SIM \
  (gst_net_sim_get_type())
#define GST_NET_SIM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  GST_TYPE_NET_SIM,GstNetSim))
#define GST_NET_SIM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  GST_TYPE_NET_SIM,GstNetSimClass))
#define GST_IS_NET_SIM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NET_SIM))
#define GST_IS_NET_SIM_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NET_SIM))

typedef struct _GstNetSim GstNetSim;
typedef struct _GstNetSimClass GstNetSimClass;
typedef struct _GstNetSimPrivate GstNetSimPrivate;

struct _GstNetSim
{
  GstElement parent;

  GstNetSimPrivate *priv;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstNetSimClass
{
  GstElementClass parent_class;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_net_sim_get_type (void);

G_END_DECLS

#endif /* __GST_NET_SIM_H__ */
