/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstbus.c: GstBus subsystem
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


#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gst_private.h"
#include "gstinfo.h"

#include "gstbus.h"

enum
{
  ARG_0,
};

static void gst_bus_class_init (GstBusClass * klass);
static void gst_bus_init (GstBus * bus);
static void gst_bus_dispose (GObject * object);

static void gst_bus_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_bus_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstObjectClass *parent_class = NULL;

/* static guint gst_bus_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_bus_get_type (void)
{
  static GType bus_type = 0;

  if (!bus_type) {
    static const GTypeInfo bus_info = {
      sizeof (GstBusClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_bus_class_init,
      NULL,
      NULL,
      sizeof (GstBus),
      0,
      (GInstanceInitFunc) gst_bus_init,
      NULL
    };

    bus_type = g_type_register_static (GST_TYPE_OBJECT, "GstBus", &bus_info, 0);
  }
  return bus_type;
}

static void
gst_bus_class_init (GstBusClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  if (!g_thread_supported ())
    g_thread_init (NULL);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_bus_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_bus_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_bus_get_property);
}

static void
gst_bus_init (GstBus * bus)
{
  bus->queue = g_async_queue_new ();

  if (socketpair (PF_UNIX, SOCK_STREAM, 0, bus->control_socket) < 0) {
    g_warning ("cannot create io channel");
  } else {
    bus->io_channel = g_io_channel_unix_new (bus->control_socket[0]);
  }
}

static void
gst_bus_dispose (GObject * object)
{
  GstBus *bus;

  bus = GST_BUS (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_bus_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBus *bus;

  bus = GST_BUS (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_bus_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBus *bus;

  bus = GST_BUS (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_bus_post (GstBus * bus, GstMessage * message)
{
  gchar c;
  GstBusSyncReply reply = GST_BUS_PASS;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);

  if (bus->sync_handler) {
    reply = bus->sync_handler (bus, message, bus->sync_handler_data);
  }

  switch (reply) {
    case GST_BUS_DROP:
      break;
    case GST_BUS_PASS:
      g_async_queue_push (bus->queue, message);
      c = 'p';
      write (bus->control_socket[1], &c, 1);
      break;
    case GST_BUS_ASYNC:
    {
      GMutex *lock = g_mutex_new ();
      GCond *cond = g_cond_new ();

      message->cond = cond;
      message->lock = lock;

      GST_DEBUG ("waiting for async delivery of message %p", message);

      g_mutex_lock (lock);
      g_async_queue_push (bus->queue, message);
      c = 'p';
      write (bus->control_socket[1], &c, 1);
      g_cond_wait (cond, lock);
      g_mutex_unlock (lock);

      GST_DEBUG ("message %p delivered asynchronously", message);

      g_mutex_free (lock);
      g_cond_free (cond);
      break;
    }
  }

  return TRUE;
}

gboolean
gst_bus_have_pending (GstBus * bus)
{
  gint length;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  length = g_async_queue_length (bus->queue);

  return (length > 0);
}

GstMessage *
gst_bus_pop (GstBus * bus)
{
  GstMessage *message;
  gchar c;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  message = g_async_queue_pop (bus->queue);
  read (bus->control_socket[0], &c, 1);

  return message;
}

void
gst_bus_set_sync_handler (GstBus * bus, GstBusSyncHandler func, gpointer data)
{
  g_return_if_fail (GST_IS_BUS (bus));

  bus->sync_handler = func;
  bus->sync_handler_data = data;
}

GSource *
gst_bus_create_watch (GstBus * bus)
{
  GSource *source;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  source = g_io_create_watch (bus->io_channel, G_IO_IN);

  return source;
}

typedef struct
{
  GSource *source;
  GstBus *bus;
  gint priority;
  GstBusHandler handler;
  gpointer user_data;
  GDestroyNotify notify;
} GstBusWatch;

static gboolean
bus_callback (GIOChannel * channel, GIOCondition cond, GstBusWatch * watch)
{
  GstMessage *message;

  g_return_val_if_fail (GST_IS_BUS (watch->bus), FALSE);

  message = gst_bus_pop (watch->bus);

  if (watch->handler)
    watch->handler (watch->bus, message, watch->user_data);

  return TRUE;
}

static void
bus_destroy (GstBusWatch * watch)
{
  g_print ("destroy\n");
  if (watch->notify) {
    watch->notify (watch->user_data);
  }
  g_free (watch);
}

guint
gst_bus_add_watch_full (GstBus * bus, gint priority,
    GstBusHandler handler, gpointer user_data, GDestroyNotify notify)
{
  guint id;
  GstBusWatch *watch;

  g_return_val_if_fail (GST_IS_BUS (bus), 0);

  watch = g_new (GstBusWatch, 1);

  watch->source = gst_bus_create_watch (bus);
  watch->bus = bus;
  watch->priority = priority;
  watch->handler = handler;
  watch->user_data = user_data;
  watch->notify = notify;

  if (priority != G_PRIORITY_DEFAULT)
    g_source_set_priority (watch->source, priority);

  g_source_set_callback (watch->source, (GSourceFunc) bus_callback, watch,
      (GDestroyNotify) bus_destroy);

  id = g_source_attach (watch->source, NULL);
  g_source_unref (watch->source);

  return id;
}

guint
gst_bus_add_watch (GstBus * bus, GstBusHandler handler, gpointer user_data)
{
  return gst_bus_add_watch_full (bus, G_PRIORITY_DEFAULT, handler, user_data,
      NULL);
}
