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
/**
 * SECTION:gstbus
 * @short_description: Asynchronous message bus subsystem
 * @see_also: #GstMessage, #GstElement
 *
 * The #GstBus is an object responsible for delivering #GstMessages in
 * a first-in first-out way from the streaming threads to the application.
 * 
 * Since the application typically only wants to deal with delivery of these
 * messages from one thread, the GstBus will marshall the messages between
 * different threads. This is important since the actual streaming of media
 * is done in another thread than the application.
 * 
 * The GstBus provides support for #GSource based notifications. This makes it
 * possible to handle the delivery in the glib mainloop.
 * 
 * A message is posted on the bus with the gst_bus_post() method. With the
 * gst_bus_peek() and _pop() methods one can look at or retrieve a previously
 * posted message.
 * 
 * The bus can be polled with the gst_bus_poll() method. This methods blocks
 * up to the specified timeout value until one of the specified messages types
 * is posted on the bus. The application can then _pop() the messages from the
 * bus to handle them.
 * Alternatively the application can register an asynchronous bus handler using
 * gst_bus_add_watch_full() orgst_bus_add_watch(). This handler will receive
 * messages a short while after they have been posted.
 * 
 * It is also possible to get messages from the bus without any thread 
 * marshalling with the gst_bus_set_sync_handler() method. This makes it
 * possible to react to a message in the same thread that posted the
 * message on the bus. This should only be used if the application is able
 * to deal with messages from different threads.
 *
 * It is important to make sure that every message is popped from the bus at
 * some point in time. Otherwise it will be presented to the watches (#GSource
 * elements) again and again. One way to implement it is having one watch with a
 * low priority (see gst_add_watch_full()) that pops all messages.
 * 
 * Every #GstElement has its own bus.
 *
 */

#include <errno.h>
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
  bus->queue = g_queue_new ();
  bus->queue_lock = g_mutex_new ();

  GST_DEBUG_OBJECT (bus, "created");

  return;
}

static void
gst_bus_dispose (GObject * object)
{
  GstBus *bus;

  bus = GST_BUS (object);

  if (bus->queue) {
    GstMessage *message;

    g_mutex_lock (bus->queue_lock);
    do {
      message = g_queue_pop_head (bus->queue);
      if (message)
        gst_message_unref (message);
    } while (message != NULL);
    g_queue_free (bus->queue);
    bus->queue = NULL;
    g_mutex_unlock (bus->queue_lock);
    g_mutex_free (bus->queue_lock);
    bus->queue_lock = NULL;
  }

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

/**
 * gst_bus_new:
 *
 * Creates a new #GstBuus instance.
 *
 * Returns: a new #GstBus instance
 */
GstBus *
gst_bus_new (void)
{
  GstBus *result;

  result = g_object_new (gst_bus_get_type (), NULL);

  return result;
}

/**
 * gst_bus_post:
 * @bus: a #GstBus to post on
 * @message: The #GstMessage to post
 *
 * Post a message on the given bus. Ownership of the message
 * is taken by the bus.
 *
 * Returns: TRUE if the message could be posted.
 *
 * MT safe.
 */
gboolean
gst_bus_post (GstBus * bus, GstMessage * message)
{
  GstBusSyncReply reply = GST_BUS_PASS;
  GstBusSyncHandler handler;
  gpointer handler_data;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);
  g_return_val_if_fail (GST_IS_MESSAGE (message), FALSE);

  GST_DEBUG_OBJECT (bus, "[msg %p] posting on bus, type %d",
      message, GST_MESSAGE_TYPE (message));

  GST_LOCK (bus);
  /* check if the bus is flushing */
  if (GST_FLAG_IS_SET (bus, GST_BUS_FLUSHING))
    goto is_flushing;

  handler = bus->sync_handler;
  handler_data = bus->sync_handler_data;
  GST_UNLOCK (bus);

  /* first call the sync handler if it is installed */
  if (handler)
    reply = handler (bus, message, handler_data);

  /* now see what we should do with the message */
  switch (reply) {
    case GST_BUS_DROP:
      /* drop the message */
      GST_DEBUG_OBJECT (bus, "[msg %p] dropped", message);
      break;
    case GST_BUS_PASS:
      /* pass the message to the async queue, refcount passed in the queue */
      GST_DEBUG_OBJECT (bus, "[msg %p] pushing on async queue", message);
      g_mutex_lock (bus->queue_lock);
      g_queue_push_tail (bus->queue, message);
      g_mutex_unlock (bus->queue_lock);
      GST_DEBUG_OBJECT (bus, "[msg %p] pushed on async queue", message);

      /* FIXME cannot assume the source is only in the default context */
      g_main_context_wakeup (NULL);

      break;
    case GST_BUS_ASYNC:
    {
      /* async delivery, we need a mutex and a cond to block
       * on */
      GMutex *lock = g_mutex_new ();
      GCond *cond = g_cond_new ();

      GST_MESSAGE_COND (message) = cond;
      GST_MESSAGE_GET_LOCK (message) = lock;

      GST_DEBUG_OBJECT (bus, "[msg %p] waiting for async delivery", message);

      /* now we lock the message mutex, send the message to the async
       * queue. When the message is handled by the app and destroyed,
       * the cond will be signalled and we can continue */
      g_mutex_lock (lock);
      g_mutex_lock (bus->queue_lock);
      g_queue_push_tail (bus->queue, message);
      g_mutex_unlock (bus->queue_lock);

      /* FIXME cannot assume the source is only in the default context */
      g_main_context_wakeup (NULL);

      /* now block till the message is freed */
      g_cond_wait (cond, lock);
      g_mutex_unlock (lock);

      GST_DEBUG_OBJECT (bus, "[msg %p] delivered asynchronously", message);

      g_mutex_free (lock);
      g_cond_free (cond);
      break;
    }
    default:
      g_warning ("invalid return from bus sync handler");
      break;
  }
  return TRUE;

  /* ERRORS */
is_flushing:
  {
    GST_DEBUG_OBJECT (bus, "bus is flushing");
    gst_message_unref (message);
    GST_UNLOCK (bus);

    return FALSE;
  }
}

/**
 * gst_bus_have_pending:
 * @bus: a #GstBus to check
 *
 * Check if there are pending messages on the bus that should be 
 * handled.
 *
 * Returns: TRUE if there are messages on the bus to be handled.
 *
 * MT safe.
 */
gboolean
gst_bus_have_pending (GstBus * bus)
{
  gint length;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  g_mutex_lock (bus->queue_lock);
  length = g_queue_get_length (bus->queue);
  g_mutex_unlock (bus->queue_lock);

  return (length > 0);
}

/**
 * gst_bus_set_flushing:
 * @bus: a #GstBus
 * @flushing: whether or not to flush the bus
 *
 * If @flushing, flush out and unref any messages queued in the bus. Releases
 * references to the message origin objects. Will flush future messages until
 * gst_bus_set_flushing() sets @flushing to #FALSE.
 *
 * MT safe.
 */
void
gst_bus_set_flushing (GstBus * bus, gboolean flushing)
{
  GstMessage *message;

  GST_LOCK (bus);

  if (flushing) {
    GST_FLAG_SET (bus, GST_BUS_FLUSHING);

    GST_DEBUG_OBJECT (bus, "set bus flushing");

    while ((message = gst_bus_pop (bus)))
      gst_message_unref (message);
  } else {
    GST_DEBUG_OBJECT (bus, "unset bus flushing");
    GST_FLAG_UNSET (bus, GST_BUS_FLUSHING);
  }

  GST_UNLOCK (bus);
}

/**
 * gst_bus_pop:
 * @bus: a #GstBus to pop
 *
 * Get a message from the bus.
 *
 * Returns: The #GstMessage that is on the bus, or NULL if the bus is empty.
 *
 * MT safe.
 */
GstMessage *
gst_bus_pop (GstBus * bus)
{
  GstMessage *message;

  g_return_val_if_fail (GST_IS_BUS (bus), NULL);

  g_mutex_lock (bus->queue_lock);
  message = g_queue_pop_head (bus->queue);
  g_mutex_unlock (bus->queue_lock);

  GST_DEBUG_OBJECT (bus, "pop on bus, got message %p", message);

  return message;
}

/**
 * gst_bus_peek:
 * @bus: a #GstBus
 *
 * Peek the message on the top of the bus' queue. The message will remain 
 * on the bus' message queue. A reference is returned, and needs to be freed
 * by the caller.
 *
 * Returns: The #GstMessage that is on the bus, or NULL if the bus is empty.
 *
 * MT safe.
 */
GstMessage *
gst_bus_peek (GstBus * bus)
{
  GstMessage *message;

  g_return_val_if_fail (GST_IS_BUS (bus), NULL);

  g_mutex_lock (bus->queue_lock);
  message = g_queue_peek_head (bus->queue);
  if (message)
    gst_message_ref (message);
  g_mutex_unlock (bus->queue_lock);

  GST_DEBUG_OBJECT (bus, "peek on bus, got message %p", message);

  return message;
}

/**
 * gst_bus_set_sync_handler:
 * @bus: a #GstBus to install the handler on
 * @func: The handler function to install
 * @data: User data that will be sent to the handler function.
 *
 * Sets the synchronous handler on the bus. The function will be called
 * every time a new message is posted on the bus. Note that the function
 * will be called in the same thread context as the posting object. This
 * function is usually only called by the creator of the bus. Applications
 * should handle messages asynchronously using the gst_bus watch and poll
 * functions.
 */
void
gst_bus_set_sync_handler (GstBus * bus, GstBusSyncHandler func, gpointer data)
{
  g_return_if_fail (GST_IS_BUS (bus));

  GST_LOCK (bus);

  /* Assert if the user attempts to replace an existing sync_handler,
   * other than to clear it */
  g_assert (func == NULL || bus->sync_handler == NULL);

  bus->sync_handler = func;
  bus->sync_handler_data = data;
  GST_UNLOCK (bus);
}

/* GSource for the bus
 */
typedef struct
{
  GSource source;
  GstBus *bus;
} GstBusSource;

static gboolean
gst_bus_source_prepare (GSource * source, gint * timeout)
{
  *timeout = -1;
  return gst_bus_have_pending (((GstBusSource *) source)->bus);
}

static gboolean
gst_bus_source_check (GSource * source)
{
  return gst_bus_have_pending (((GstBusSource *) source)->bus);
}

static gboolean
gst_bus_source_dispatch (GSource * source, GSourceFunc callback,
    gpointer user_data)
{
  GstBusHandler handler = (GstBusHandler) callback;
  GstBusSource *bsource = (GstBusSource *) source;
  GstMessage *message;
  gboolean needs_pop = TRUE;
  GstBus *bus;

  g_return_val_if_fail (bsource != NULL, FALSE);

  bus = bsource->bus;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  message = gst_bus_peek (bus);

  GST_DEBUG_OBJECT (bus, "source %p have message %p", source, message);

  g_return_val_if_fail (message != NULL, TRUE);

  if (!handler)
    goto no_handler;

  GST_DEBUG_OBJECT (bus, "source %p calling dispatch with %p", source, message);

  needs_pop = handler (bus, message, user_data);
  gst_message_unref (message);

  GST_DEBUG_OBJECT (bus, "source %p handler returns %d", source, needs_pop);
  if (needs_pop) {
    message = gst_bus_pop (bus);
    if (message) {
      gst_message_unref (message);
    } else {
      /* after executing the handler, the app could have disposed
       * the pipeline and set the bus to flushing. It is possible
       * then that there are no more messages on the bus. this is
       * not a problem. */
      GST_DEBUG ("handler requested pop but no message on the bus");
    }
  }
  return TRUE;

no_handler:
  {
    g_warning ("GstBus watch dispatched without callback\n"
        "You must call g_source_connect().");
    gst_message_unref (message);
    return FALSE;
  }
}

static void
gst_bus_source_finalize (GSource * source)
{
  GstBusSource *bsource = (GstBusSource *) source;

  gst_object_unref (bsource->bus);
  bsource->bus = NULL;
}

static GSourceFuncs gst_bus_source_funcs = {
  gst_bus_source_prepare,
  gst_bus_source_check,
  gst_bus_source_dispatch,
  gst_bus_source_finalize
};

/**
 * gst_bus_create_watch:
 * @bus: a #GstBus to create the watch for
 *
 * Create watch for this bus. 
 *
 * Returns: A #GSource that can be added to a mainloop.
 */
GSource *
gst_bus_create_watch (GstBus * bus)
{
  GstBusSource *source;

  g_return_val_if_fail (GST_IS_BUS (bus), NULL);

  source = (GstBusSource *) g_source_new (&gst_bus_source_funcs,
      sizeof (GstBusSource));
  gst_object_ref (bus);
  source->bus = bus;

  return (GSource *) source;
}

/**
 * gst_bus_add_watch_full:
 * @bus: a #GstBus to create the watch for.
 * @priority: The priority of the watch.
 * @handler: A function to call when a message is received.
 * @user_data: user data passed to @handler.
 * @notify: the function to call when the source is removed.
 *
 * Adds the bus to the mainloop with the given priority. If the handler returns
 * TRUE, the message will then be popped off the queue. When the handler is
 * called, the message belongs to the caller; if you want to keep a copy of it,
 * call gst_message_ref before leaving the handler.
 *
 * Returns: The event source id.
 *
 * MT safe.
 */
guint
gst_bus_add_watch_full (GstBus * bus, gint priority,
    GstBusHandler handler, gpointer user_data, GDestroyNotify notify)
{
  guint id;
  GSource *source;

  g_return_val_if_fail (GST_IS_BUS (bus), 0);

  source = gst_bus_create_watch (bus);

  if (priority != G_PRIORITY_DEFAULT)
    g_source_set_priority (source, priority);

  g_source_set_callback (source, (GSourceFunc) handler, user_data, notify);

  id = g_source_attach (source, NULL);
  g_source_unref (source);

  GST_DEBUG_OBJECT (bus, "New source %p", source);
  return id;
}

/**
 * gst_bus_add_watch:
 * @bus: a #GstBus to create the watch for
 * @handler: A function to call when a message is received.
 * @user_data: user data passed to @handler.
 *
 * Adds the bus to the mainloop with the default priority.
 *
 * Returns: The event source id.
 *
 * MT safe.
 */
guint
gst_bus_add_watch (GstBus * bus, GstBusHandler handler, gpointer user_data)
{
  return gst_bus_add_watch_full (bus, G_PRIORITY_DEFAULT, handler, user_data,
      NULL);
}

typedef struct
{
  GMainLoop *loop;
  guint timeout_id;
  gboolean source_running;
  GstMessageType events;
  GstMessageType revent;
} GstBusPollData;

static gboolean
poll_handler (GstBus * bus, GstMessage * message, GstBusPollData * poll_data)
{
  if (!g_main_loop_is_running (poll_data->loop))
    return FALSE;

  if (GST_MESSAGE_TYPE (message) & poll_data->events) {
    poll_data->revent = GST_MESSAGE_TYPE (message);
    g_main_loop_quit (poll_data->loop);

    /* keep the message on the queue */
    return FALSE;
  } else {
    /* pop and unref the message */
    return TRUE;
  }
}

static gboolean
poll_timeout (GstBusPollData * poll_data)
{
  g_main_loop_quit (poll_data->loop);

  /* returning FALSE will remove the source id */
  return FALSE;
}

static void
poll_destroy (GstBusPollData * poll_data)
{
  poll_data->source_running = FALSE;
  if (!poll_data->timeout_id) {
    g_main_loop_unref (poll_data->loop);
    g_free (poll_data);
  }
}

static void
poll_destroy_timeout (GstBusPollData * poll_data)
{
  poll_data->timeout_id = 0;
  if (!poll_data->source_running) {
    g_main_loop_unref (poll_data->loop);
    g_free (poll_data);
  }
}

/**
 * gst_bus_poll:
 * @bus: a #GstBus
 * @events: a mask of #GstMessageType, representing the set of message types to
 * poll for.
 * @timeout: the poll timeout, as a #GstClockTimeDiff, or -1 to poll indefinitely.
 *
 * Poll the bus for events. Will block while waiting for events to come. You can
 * specify a maximum time to poll with the @timeout parameter. If @timeout is
 * negative, this function will block indefinitely.
 *
 * This function will enter the default mainloop while polling.
 *
 * Returns: The type of the message that was received, or GST_MESSAGE_UNKNOWN if
 * the poll timed out. The message will remain in the bus queue; you will need
 * to gst_bus_pop() it off before entering gst_bus_poll() again.
 */
GstMessageType
gst_bus_poll (GstBus * bus, GstMessageType events, GstClockTimeDiff timeout)
{
  GstBusPollData *poll_data;
  GstMessageType ret;
  guint id;

  poll_data = g_new0 (GstBusPollData, 1);
  g_return_val_if_fail (poll_data != NULL, GST_MESSAGE_UNKNOWN);

  poll_data->source_running = TRUE;
  poll_data->loop = g_main_loop_new (NULL, FALSE);
  poll_data->events = events;
  poll_data->revent = GST_MESSAGE_UNKNOWN;

  if (timeout >= 0)
    poll_data->timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
        timeout / GST_MSECOND, (GSourceFunc) poll_timeout, poll_data,
        (GDestroyNotify) poll_destroy_timeout);
  else
    poll_data->timeout_id = 0;

  id = gst_bus_add_watch_full (bus, G_PRIORITY_DEFAULT_IDLE,
      (GstBusHandler) poll_handler, poll_data, (GDestroyNotify) poll_destroy);
  g_main_loop_run (poll_data->loop);
  ret = poll_data->revent;

  if (poll_data->timeout_id)
    g_source_remove (poll_data->timeout_id);

  /* poll_data may get destroyed at any time now */
  g_source_remove (id);

  GST_DEBUG_OBJECT (bus, "finished poll with messagetype %d", ret);

  return ret;
}
