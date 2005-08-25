/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstbus.h: Header for GstBus subsystem
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

#ifndef __GST_BUS_H__
#define __GST_BUS_H__

typedef struct _GstBus GstBus;
typedef struct _GstBusClass GstBusClass;

#include <gst/gstmessage.h>
#include <gst/gstclock.h>

G_BEGIN_DECLS

/* --- standard type macros --- */
#define GST_TYPE_BUS              (gst_bus_get_type ())
#define GST_BUS(bus)              (G_TYPE_CHECK_INSTANCE_CAST ((bus), GST_TYPE_BUS, GstBus))
#define GST_IS_BUS(bus)           (G_TYPE_CHECK_INSTANCE_TYPE ((bus), GST_TYPE_BUS))
#define GST_BUS_CLASS(bclass)     (G_TYPE_CHECK_CLASS_CAST ((bclass), GST_TYPE_BUS, GstBusClass))
#define GST_IS_BUS_CLASS(bclass)  (G_TYPE_CHECK_CLASS_TYPE ((bclass), GST_TYPE_BUS))
#define GST_BUS_GET_CLASS(bus)    (G_TYPE_INSTANCE_GET_CLASS ((bus), GST_TYPE_BUS, GstBusClass))
#define GST_BUS_CAST(bus)         ((GstBus*)(bus))

typedef enum {
  GST_BUS_FLUSHING		= GST_OBJECT_FLAG_LAST,

  GST_BUS_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 1
} GstBusFlags;

typedef enum
{
  GST_BUS_DROP = 0,             /* drop message */
  GST_BUS_PASS = 1,             /* pass message to async queue */
  GST_BUS_ASYNC = 2,            /* pass message to async queue, continue if message is handled */
} GstBusSyncReply;

/**
 * GstBusSyncHandler:
 * @bus: the #GstBus that sent the message
 * @messages: the #GstMessage
 * @data: user data that has been given, when registering the handler
 *
 * Handler will be invoked synchronously, when a new message has been injected
 * into the bus.
 *
 * Returns: #GstBusSyncReply stating what to do with the message
 */
typedef GstBusSyncReply (*GstBusSyncHandler) 	(GstBus * bus, GstMessage * message, gpointer data);
/**
 * GstBusHandler:
 * @bus: the #GstBus that sent the message
 * @messages: the #GstMessage
 * @data: user data that has been given, when registering the handler
 *
 * Handler will be invoked asynchronously, after a new message has been injected
 * into the bus.
 *
 * Returns: %TRUE if message should be taken from the bus
 */
typedef gboolean 	(*GstBusHandler) 	(GstBus * bus, GstMessage * message, gpointer data);

struct _GstBus
{
  GstObject 	    object;

  /*< private > */
  GQueue           *queue;
  GMutex           *queue_lock;

  GstBusSyncHandler sync_handler;
  gpointer 	    sync_handler_data;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBusClass
{
  GstObjectClass parent_class;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

GType 			gst_bus_get_type 		(void);

GstBus*			gst_bus_new	 		(void);

gboolean 		gst_bus_post 			(GstBus * bus, GstMessage * message);

gboolean 		gst_bus_have_pending 		(GstBus * bus);
GstMessage *		gst_bus_peek 			(GstBus * bus);
GstMessage *		gst_bus_pop 			(GstBus * bus);
void			gst_bus_set_flushing		(GstBus * bus, gboolean flushing);

void 			gst_bus_set_sync_handler 	(GstBus * bus, GstBusSyncHandler func,
    							 gpointer data);

GSource *		gst_bus_create_watch 		(GstBus * bus);
guint 			gst_bus_add_watch_full 		(GstBus * bus,
    							 gint priority,
    							 GstBusHandler handler, 
							 gpointer user_data, 
							 GDestroyNotify notify);
guint 			gst_bus_add_watch 		(GstBus * bus,
    							 GstBusHandler handler, 
							 gpointer user_data);
GstMessageType		gst_bus_poll			(GstBus *bus, GstMessageType events,
                                                         GstClockTimeDiff timeout);

G_END_DECLS

#endif /* __GST_BUS_H__ */
