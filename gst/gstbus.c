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
 * The #GSource callback function gst_bus_async_signal_func() can be used to
 * convert all bus messages into signal emissions.
 *
 * A message is posted on the bus with the gst_bus_post() method. With the
 * gst_bus_peek() and gst_bus_pop() methods one can look at or retrieve a previously
 * posted message.
 *
 * The bus can be polled with the gst_bus_poll() method. This methods blocks
 * up to the specified timeout value until one of the specified messages types
 * is posted on the bus. The application can then _pop() the messages from the
 * bus to handle them.
 * Alternatively the application can register an asynchronous bus function
 * using gst_bus_add_watch_full() or gst_bus_add_watch(). This function will
 * receive messages a short while after they have been posted.
 *
 * It is also possible to get messages from the bus without any thread
 * marshalling with the gst_bus_set_sync_handler() method. This makes it
 * possible to react to a message in the same thread that posted the
 * message on the bus. This should only be used if the application is able
 * to deal with messages from different threads.
 *
 * Every #GstPipeline has one bus.
 *
 * Note that a #GstPipeline will set its bus into flushing state when changing
 * from READY to NULL state.
 *
 * Last reviewed on 2005-10-28 (0.9.4)
 */

#include <errno.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/types.h>

#include "gst_private.h"
#include "gstinfo.h"

#include "gstbus.h"

#define GST_CAT_DEFAULT GST_CAT_BUS
/* bus signals */
enum
{
  SYNC_MESSAGE,
  ASYNC_MESSAGE,
  /* add more above */
  LAST_SIGNAL
};

static void gst_bus_class_init (GstBusClass * klass);
static void gst_bus_init (GstBus * bus);
static void gst_bus_dispose (GObject * object);

static void gst_bus_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_bus_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstObjectClass *parent_class = NULL;
static guint gst_bus_signals[LAST_SIGNAL] = { 0 };

/* the context we wakeup when we posted a message on the bus */
static GMainContext *main_context;

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

/* fixme: do something about this */
static void
marshal_VOID__MINIOBJECT (GClosure * closure, GValue * return_value,
    guint n_param_values, const GValue * param_values, gpointer invocation_hint,
    gpointer marshal_data)
{
  typedef void (*marshalfunc_VOID__MINIOBJECT) (gpointer obj, gpointer arg1,
      gpointer data2);
  register marshalfunc_VOID__MINIOBJECT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (marshalfunc_VOID__MINIOBJECT) (marshal_data ? marshal_data : cc->
      callback);

  callback (data1, gst_value_get_mini_object (param_values + 1), data2);
}

static void
gst_bus_class_init (GstBusClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  if (!g_thread_supported ())
    g_thread_init (NULL);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_bus_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_bus_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_bus_get_property);

  /**
   * GstBus::sync-message:
   * @bus: the object which received the signal
   * @message: the message that has been posted synchronously
   *
   * A message has been posted on the bus. This signal is emitted from the
   * thread that posted the message so one has to be carefull with locking.
   */
  gst_bus_signals[SYNC_MESSAGE] =
      g_signal_new ("sync-message", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET (GstBusClass, sync_message), NULL, NULL,
      marshal_VOID__MINIOBJECT, G_TYPE_NONE, 1, GST_TYPE_MESSAGE);

  /**
   * GstBus::message:
   * @bus: the object which received the signal
   * @message: the message that has been posted asynchronously
   *
   * A message has been posted on the bus. This signal is emitted from a
   * GSource added to the mainloop.
   */
  gst_bus_signals[ASYNC_MESSAGE] =
      g_signal_new ("message", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET (GstBusClass, message), NULL, NULL,
      marshal_VOID__MINIOBJECT, G_TYPE_NONE, 1, GST_TYPE_MESSAGE);

  main_context = g_main_context_default ();
}

static void
gst_bus_init (GstBus * bus)
{
  bus->queue = g_queue_new ();
  bus->queue_lock = g_mutex_new ();

  GST_DEBUG_OBJECT (bus, "created");
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
 * Creates a new #GstBus instance.
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
 * Returns: TRUE if the message could be posted, FALSE if the bus is flushing.
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

  GST_DEBUG_OBJECT (bus, "[msg %p] posting on bus, type %s",
      message, gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

  GST_OBJECT_LOCK (bus);
  /* check if the bus is flushing */
  if (GST_OBJECT_FLAG_IS_SET (bus, GST_BUS_FLUSHING))
    goto is_flushing;

  handler = bus->sync_handler;
  handler_data = bus->sync_handler_data;
  GST_OBJECT_UNLOCK (bus);

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

      /* FIXME cannot assume sources are only in the default context */
      g_main_context_wakeup (main_context);

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

      /* FIXME cannot assume sources are only in the default context */
      g_main_context_wakeup (main_context);

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
    GST_OBJECT_UNLOCK (bus);

    return FALSE;
  }
}

/**
 * gst_bus_have_pending:
 * @bus: a #GstBus to check
 *
 * Check if there are pending messages on the bus that
 * should be handled.
 *
 * Returns: TRUE if there are messages on the bus to be handled, FALSE 
 * otherwise.
 *
 * MT safe.
 */
gboolean
gst_bus_have_pending (GstBus * bus)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  g_mutex_lock (bus->queue_lock);
  /* see if there is a message on the bus */
  result = !g_queue_is_empty (bus->queue);
  g_mutex_unlock (bus->queue_lock);

  return result;
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

  GST_OBJECT_LOCK (bus);

  if (flushing) {
    GST_OBJECT_FLAG_SET (bus, GST_BUS_FLUSHING);

    GST_DEBUG_OBJECT (bus, "set bus flushing");

    while ((message = gst_bus_pop (bus)))
      gst_message_unref (message);
  } else {
    GST_DEBUG_OBJECT (bus, "unset bus flushing");
    GST_OBJECT_FLAG_UNSET (bus, GST_BUS_FLUSHING);
  }

  GST_OBJECT_UNLOCK (bus);
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
  if (message)
    GST_DEBUG_OBJECT (bus, "pop from bus, have %d messages, got message %p, %s",
        g_queue_get_length (bus->queue) + 1, message,
        gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
  else
    GST_DEBUG_OBJECT (bus, "pop from bus, no messages");
  g_mutex_unlock (bus->queue_lock);

  return message;
}

/**
 * gst_bus_peek:
 * @bus: a #GstBus
 *
 * Peek the message on the top of the bus' queue. The message will remain
 * on the bus' message queue. A reference is returned, and needs to be unreffed
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
 *
 * You cannot replace an existing sync_handler. You can pass NULL to this
 * function, which will clear the existing handler.
 */
void
gst_bus_set_sync_handler (GstBus * bus, GstBusSyncHandler func, gpointer data)
{
  g_return_if_fail (GST_IS_BUS (bus));

  GST_OBJECT_LOCK (bus);

  /* Assert if the user attempts to replace an existing sync_handler,
   * other than to clear it */
  if (func != NULL && bus->sync_handler != NULL)
    goto no_replace;

  bus->sync_handler = func;
  bus->sync_handler_data = data;
  GST_OBJECT_UNLOCK (bus);

  return;

no_replace:
  {
    GST_OBJECT_UNLOCK (bus);
    g_warning ("cannot replace existing sync handler");
    return;
  }
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
  GstBusSource *bsrc = (GstBusSource *) source;

  *timeout = -1;
  return gst_bus_have_pending (bsrc->bus);
}

static gboolean
gst_bus_source_check (GSource * source)
{
  GstBusSource *bsrc = (GstBusSource *) source;

  return gst_bus_have_pending (bsrc->bus);
}

static gboolean
gst_bus_source_dispatch (GSource * source, GSourceFunc callback,
    gpointer user_data)
{
  GstBusFunc handler = (GstBusFunc) callback;
  GstBusSource *bsource = (GstBusSource *) source;
  GstMessage *message;
  gboolean keep;
  GstBus *bus;

  g_return_val_if_fail (bsource != NULL, FALSE);

  bus = bsource->bus;

  g_return_val_if_fail (GST_IS_BUS (bus), FALSE);

  message = gst_bus_pop (bus);
  g_return_val_if_fail (message != NULL, FALSE);

  if (!handler)
    goto no_handler;

  GST_DEBUG_OBJECT (bus, "source %p calling dispatch with %p", source, message);

  keep = handler (bus, message, user_data);
  gst_message_unref (message);

  GST_DEBUG_OBJECT (bus, "source %p handler returns %d", source, keep);

  return keep;

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
 * Create watch for this bus. The GSource will be dispatched whenever
 * a message is on the bus. After the GSource is dispatched, the
 * message is popped off the bus and unreffed.
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
 * @func: A function to call when a message is received.
 * @user_data: user data passed to @func.
 * @notify: the function to call when the source is removed.
 *
 * Adds a bus watch to the default main context with the given priority.
 * If the func returns FALSE, the source will be removed.
 *
 * When the func is called, the message belongs to the caller; if you want to
 * keep a copy of it, call gst_message_ref() before leaving the func.
 *
 * The watch can be removed using #g_source_remove().
 *
 * Returns: The event source id.
 *
 * MT safe.
 */
guint
gst_bus_add_watch_full (GstBus * bus, gint priority,
    GstBusFunc func, gpointer user_data, GDestroyNotify notify)
{
  guint id;
  GSource *source;

  g_return_val_if_fail (GST_IS_BUS (bus), 0);

  source = gst_bus_create_watch (bus);

  if (priority != G_PRIORITY_DEFAULT)
    g_source_set_priority (source, priority);

  g_source_set_callback (source, (GSourceFunc) func, user_data, notify);

  id = g_source_attach (source, NULL);
  g_source_unref (source);

  GST_DEBUG_OBJECT (bus, "New source %p", source);
  return id;
}

/**
 * gst_bus_add_watch:
 * @bus: a #GstBus to create the watch for
 * @func: A function to call when a message is received.
 * @user_data: user data passed to @func.
 *
 * Adds a bus watch to the default main context with the default priority.
 *
 * The watch can be removed using #g_source_remove().
 *
 * Returns: The event source id.
 *
 * MT safe.
 */
guint
gst_bus_add_watch (GstBus * bus, GstBusFunc func, gpointer user_data)
{
  return gst_bus_add_watch_full (bus, G_PRIORITY_DEFAULT, func,
      user_data, NULL);
}

typedef struct
{
  GMainLoop *loop;
  guint timeout_id;
  gboolean source_running;
  GstMessageType events;
  GstMessage *message;
} GstBusPollData;

static void
poll_func (GstBus * bus, GstMessage * message, GstBusPollData * poll_data)
{
  if (!g_main_loop_is_running (poll_data->loop)) {
    GST_DEBUG ("mainloop %p not running", poll_data->loop);
    return;
  }

  if (GST_MESSAGE_TYPE (message) & poll_data->events) {
    g_return_if_fail (poll_data->message == NULL);
    /* keep ref to message */
    poll_data->message = gst_message_ref (message);
    GST_DEBUG ("mainloop %p quit", poll_data->loop);
    g_main_loop_quit (poll_data->loop);
  }
}

static gboolean
poll_timeout (GstBusPollData * poll_data)
{
  GST_DEBUG ("mainloop %p quit", poll_data->loop);
  g_main_loop_quit (poll_data->loop);

  /* we don't remove the GSource as this would free our poll_data,
   * which we still need */
  return TRUE;
}

static void
poll_destroy (GstBusPollData * poll_data, gpointer unused)
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
 * Poll the bus for messages. Will block while waiting for messages to come.
 * You can specify a maximum time to poll with the @timeout parameter. If
 * @timeout is negative, this function will block indefinitely.
 *
 * All messages not in @events will be popped off the bus and will be ignored.
 *
 * Because poll is implemented using the "message" signal enabled by
 * gst_bus_add_signal_watch(), calling gst_bus_poll() will cause the "message"
 * signal to be emitted for every message that poll sees. Thus a "message"
 * signal handler will see the same messages that this function sees -- neither
 * will steal messages from the other.
 *
 * This function will run a main loop from the default main context when
 * polling.
 *
 * Returns: The message that was received, or NULL if the poll timed out.
 * The message is taken from the bus and needs to be unreffed after usage.
 */
GstMessage *
gst_bus_poll (GstBus * bus, GstMessageType events, GstClockTimeDiff timeout)
{
  GstBusPollData *poll_data;
  GstMessage *ret;
  gulong id;

  poll_data = g_new0 (GstBusPollData, 1);
  poll_data->source_running = TRUE;
  poll_data->loop = g_main_loop_new (NULL, FALSE);
  poll_data->events = events;
  poll_data->message = NULL;

  if (timeout >= 0)
    poll_data->timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
        timeout / GST_MSECOND, (GSourceFunc) poll_timeout, poll_data,
        (GDestroyNotify) poll_destroy_timeout);
  else
    poll_data->timeout_id = 0;

  id = g_signal_connect_data (bus, "message", G_CALLBACK (poll_func), poll_data,
      (GClosureNotify) poll_destroy, 0);

  /* these can be nested, so it's ok */
  gst_bus_add_signal_watch (bus);

  GST_DEBUG ("running mainloop %p", poll_data->loop);
  g_main_loop_run (poll_data->loop);
  GST_DEBUG ("mainloop stopped %p", poll_data->loop);

  gst_bus_remove_signal_watch (bus);

  /* holds a ref */
  ret = poll_data->message;

  if (poll_data->timeout_id)
    g_source_remove (poll_data->timeout_id);

  /* poll_data will be freed now */
  g_signal_handler_disconnect (bus, id);

  GST_DEBUG_OBJECT (bus, "finished poll with message %p", ret);

  return ret;
}

/**
 * gst_bus_async_signal_func:
 * @bus: a #GstBus
 * @message: the #GstMessage received
 * @data: user data
 *
 * A helper GstBusFunc that can be used to convert all asynchronous messages
 * into signals.
 *
 * Returns: TRUE
 */
gboolean
gst_bus_async_signal_func (GstBus * bus, GstMessage * message, gpointer data)
{
  GQuark detail = 0;

  g_return_val_if_fail (GST_IS_BUS (bus), TRUE);
  g_return_val_if_fail (message != NULL, TRUE);

  detail = gst_message_type_to_quark (GST_MESSAGE_TYPE (message));

  g_signal_emit (bus, gst_bus_signals[ASYNC_MESSAGE], detail, message);

  /* we never remove this source based on signal emission return values */
  return TRUE;
}

/**
 * gst_bus_sync_signal_handler:
 * @bus: a #GstBus
 * @message: the #GstMessage received
 * @data: user data
 *
 * A helper GstBusSyncHandler that can be used to convert all synchronous
 * messages into signals.
 *
 * Returns: GST_BUS_PASS
 */
GstBusSyncReply
gst_bus_sync_signal_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  GQuark detail = 0;

  g_return_val_if_fail (GST_IS_BUS (bus), GST_BUS_DROP);
  g_return_val_if_fail (message != NULL, GST_BUS_DROP);

  detail = gst_message_type_to_quark (GST_MESSAGE_TYPE (message));

  g_signal_emit (bus, gst_bus_signals[SYNC_MESSAGE], detail, message);

  return GST_BUS_PASS;
}

/**
 * gst_bus_add_signal_watch_full:
 * @bus: a #GstBus on which you want to recieve the "message" signal
 * @priority: The priority of the watch.
 *
 * Adds a bus signal watch to the default main context with the given priority.
 * After calling this statement, the bus will emit the message signal for each
 * message posted on the bus.
 *
 * This function may be called multiple times. To clean up, the caller is
 * responsible for calling gst_bus_remove_signal_watch() as many times as this
 * function is called.
 *
 * MT safe.
 */
void
gst_bus_add_signal_watch_full (GstBus * bus, gint priority)
{
  g_return_if_fail (GST_IS_BUS (bus));

  /* I know the callees don't take this lock, so go ahead and abuse it */
  GST_OBJECT_LOCK (bus);

  if (bus->num_signal_watchers > 0)
    goto done;

  g_assert (bus->signal_watch_id == 0);

  bus->signal_watch_id =
      gst_bus_add_watch_full (bus, priority, gst_bus_async_signal_func, NULL,
      NULL);

done:

  bus->num_signal_watchers++;

  GST_OBJECT_UNLOCK (bus);
}

/**
 * gst_bus_add_signal_watch:
 * @bus: a #GstBus on which you want to recieve the "message" signal
 *
 * Adds a bus signal watch to the default main context with the default priority.
 * After calling this statement, the bus will emit the message signal for each
 * message posted on the bus.
 *
 * This function may be called multiple times. To clean up, the caller is
 * responsible for calling gst_bus_remove_signal_watch() as many times as this
 * function is called.
 *
 * MT safe.
 */
void
gst_bus_add_signal_watch (GstBus * bus)
{
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_DEFAULT);
}

/**
 * gst_bus_remove_signal_watch:
 * @bus: a #GstBus you previously added a signal watch to
 *
 * Removes a signal watch previously added with gst_bus_add_signal_watch().
 *
 * MT safe.
 */
void
gst_bus_remove_signal_watch (GstBus * bus)
{
  g_return_if_fail (GST_IS_BUS (bus));

  /* I know the callees don't take this lock, so go ahead and abuse it */
  GST_OBJECT_LOCK (bus);

  if (bus->num_signal_watchers == 0)
    goto error;

  bus->num_signal_watchers--;

  if (bus->num_signal_watchers > 0)
    goto done;

  g_source_remove (bus->signal_watch_id);
  bus->signal_watch_id = 0;

done:
  GST_OBJECT_UNLOCK (bus);
  return;

error:
  {
    g_critical ("Bus %s has no signal watches attached", GST_OBJECT_NAME (bus));
    GST_OBJECT_UNLOCK (bus);
    return;
  }
}
