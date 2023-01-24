---
title: Threads
...

# Threads

GStreamer is inherently multi-threaded, and is fully thread-safe. Most
threading internals are hidden from the application, which should make
application development easier. However, in some cases, applications may
want to have influence on some parts of those. GStreamer allows
applications to force the use of multiple threads over some parts of a
pipeline. See [When would you want to force a
thread?](#when-would-you-want-to-force-a-thread).

GStreamer can also notify you when threads are created so that you can
configure things such as the thread priority or the threadpool to use.
See [Configuring Threads in
GStreamer](#configuring-threads-in-gstreamer).

## Scheduling in GStreamer

Each element in the GStreamer pipeline decides how it is going to be
scheduled. Elements can choose if their pads are to be scheduled
push-based or pull-based. An element can, for example, choose to start a
thread to start pulling from the sink pad or/and start pushing on the
source pad. An element can also choose to use the upstream or downstream
thread for its data processing in push and pull mode respectively.
GStreamer does not pose any restrictions on how the element chooses to
be scheduled. See the Plugin Writer Guide for more details.

What will happen in any case is that some elements will start a thread
for their data processing, called the “streaming threads”. The streaming
threads, or `GstTask` objects, are created from a `GstTaskPool` when the
element needs to make a streaming thread. In the next section we see how
we can receive notifications of the tasks and pools.

## Configuring Threads in GStreamer

A `STREAM_STATUS` message is posted on the bus to inform you about the
status of the streaming threads. You will get the following information
from the message:

  - When a new thread is about to be created, you will be notified of
    this with a `GST_STREAM_STATUS_TYPE_CREATE` type. It is then
    possible to configure a `GstTaskPool` in the `GstTask`. The custom
    taskpool will provide custom threads for the task to implement the
    streaming threads.

    This message needs to be handled synchronously if you want to
    configure a custom taskpool. If you don't configure the taskpool on
    the task when this message returns, the task will use its default
    pool.

  - When a thread is entered or left. This is the moment where you could
    configure thread priorities. You also get a notification when a
    thread is destroyed.

  - You get messages when the thread starts, pauses and stops. This
    could be used to visualize the status of streaming threads in a gui
    application.

We will now look at some examples in the next sections.

### Boost priority of a thread

```
.----------.    .----------.
| fakesrc  |    | fakesink |
|         src->sink        |
'----------'    '----------'

```

Let's look at the simple pipeline above. We would like to boost the
priority of the streaming thread. It will be the fakesrc element that
starts the streaming thread for generating the fake data pushing them to
the peer fakesink. The flow for changing the priority would go like
this:

  - When going from `READY` to `PAUSED` state, fakesrc will require a
    streaming thread for pushing data into the fakesink. It will post a
    `STREAM_STATUS` message indicating its requirement for a streaming
    thread.

  - The application will react to the `STREAM_STATUS` messages with a
    sync bus handler. It will then configure a custom `GstTaskPool` on
    the `GstTask` inside the message. The custom taskpool is responsible
    for creating the threads. In this example we will make a thread with
    a higher priority.

  - Alternatively, since the sync message is called in the thread
    context, you can use thread `ENTER`/`LEAVE` notifications to change the
    priority or scheduling policy of the current thread.

In a first step we need to implement a custom `GstTaskPool` that we can
configure on the task. Below is the implementation of a `GstTaskPool`
subclass that uses pthreads to create a `SCHED_RR` real-time thread. Note
that creating real-time threads might require extra privileges.

``` c
#include <pthread.h>

typedef struct
{
  pthread_t thread;
} TestRTId;

G_DEFINE_TYPE (TestRTPool, test_rt_pool, GST_TYPE_TASK_POOL);

static void
default_prepare (GstTaskPool * pool, GError ** error)
{
  /* we don't do anything here. We could construct a pool of threads here that
   * we could reuse later but we don't */
}

static void
default_cleanup (GstTaskPool * pool)
{
}

static gpointer
default_push (GstTaskPool * pool, GstTaskPoolFunction func, gpointer data,
    GError ** error)
{
  TestRTId *tid;
  gint res;
  pthread_attr_t attr;
  struct sched_param param;

  tid = g_new0 (TestRTId, 1);

  pthread_attr_init (&attr);
  if ((res = pthread_attr_setschedpolicy (&attr, SCHED_RR)) != 0)
    g_warning ("setschedpolicy: failure: %p", g_strerror (res));

  param.sched_priority = 50;
  if ((res = pthread_attr_setschedparam (&attr, &param)) != 0)
    g_warning ("setschedparam: failure: %p", g_strerror (res));

  if ((res = pthread_attr_setinheritsched (&attr, PTHREAD_EXPLICIT_SCHED)) != 0)
    g_warning ("setinheritsched: failure: %p", g_strerror (res));

  res = pthread_create (&tid->thread, &attr, (void *(*)(void *)) func, data);

  if (res != 0) {
    g_set_error (error, G_THREAD_ERROR, G_THREAD_ERROR_AGAIN,
        "Error creating thread: %s", g_strerror (res));
    g_free (tid);
    tid = NULL;
  }

  return tid;
}

static void
default_join (GstTaskPool * pool, gpointer id)
{
  TestRTId *tid = (TestRTId *) id;

  pthread_join (tid->thread, NULL);

  g_free (tid);
}

static void
test_rt_pool_class_init (TestRTPoolClass * klass)
{
  GstTaskPoolClass *gsttaskpool_class;

  gsttaskpool_class = (GstTaskPoolClass *) klass;

  gsttaskpool_class->prepare = default_prepare;
  gsttaskpool_class->cleanup = default_cleanup;
  gsttaskpool_class->push = default_push;
  gsttaskpool_class->join = default_join;
}

static void
test_rt_pool_init (TestRTPool * pool)
{
}

GstTaskPool *
test_rt_pool_new (void)
{
  GstTaskPool *pool;

  pool = g_object_new (TEST_TYPE_RT_POOL, NULL);

  return pool;
}
```

The important function to implement when writing an taskpool is the
“push” function. The implementation should start a thread that calls
the given function. More involved implementations might want to keep
some threads around in a pool because creating and destroying threads is
not always the fastest operation.

In a next step we need to actually configure the custom taskpool when
the fakesrc needs it. For this we intercept the `STREAM_STATUS` messages
with a sync handler.

``` c
static GMainLoop* loop;

static void
on_stream_status (GstBus     *bus,
                  GstMessage *message,
                  gpointer    user_data)
{
  GstStreamStatusType type;
  GstElement *owner;
  const GValue *val;
  GstTask *task = NULL;

  gst_message_parse_stream_status (message, &type, &owner);

  val = gst_message_get_stream_status_object (message);

  /* see if we know how to deal with this object */
  if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
    task = g_value_get_object (val);
  }

  switch (type) {
    case GST_STREAM_STATUS_TYPE_CREATE:
      if (task) {
        GstTaskPool *pool;

        pool = test_rt_pool_new();

        gst_task_set_pool (task, pool);
      }
      break;
    default:
      break;
  }
}

static void
on_error (GstBus     *bus,
          GstMessage *message,
          gpointer    user_data)
{
  g_message ("received ERROR");
  g_main_loop_quit (loop);
}

static void
on_eos (GstBus     *bus,
        GstMessage *message,
        gpointer    user_data)
{
  g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
  GstElement *bin, *fakesrc, *fakesink;
  GstBus *bus;
  GstStateChangeReturn ret;

  gst_init (&argc, &argv);

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  /* create a source */
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_assert (fakesrc);
  g_object_set (fakesrc, "num-buffers", 50, NULL);

  /* and a sink */
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (fakesink);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), fakesrc, fakesink, NULL);

  /* link the elements */
  gst_element_link (fakesrc, fakesink);

  loop = g_main_loop_new (NULL, FALSE);

  /* get the bus, we need to install a sync handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (bin));
  gst_bus_enable_sync_message_emission (bus);
  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "sync-message::stream-status",
      (GCallback) on_stream_status, NULL);
  g_signal_connect (bus, "message::error",
      (GCallback) on_error, NULL);
  g_signal_connect (bus, "message::eos",
      (GCallback) on_eos, NULL);

  /* start playing */
  ret = gst_element_set_state (bin, GST_STATE_PLAYING);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_message ("failed to change state");
    return -1;
  }

  /* Run event loop listening for bus messages until EOS or ERROR */
  g_main_loop_run (loop);

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bus);
  g_main_loop_unref (loop);

  return 0;
}
```

Note that this program likely needs root permissions in order to create
real-time threads. When the thread can't be created, the state change
function will fail, which we catch in the application above.

When there are multiple threads in the pipeline, you will receive
multiple `STREAM_STATUS` messages. You should use the owner of the
message, which is likely the pad or the element that starts the thread,
to figure out what the function of this thread is in the context of the
application.

## When would you want to force a thread?

We have seen that threads are created by elements but it is also
possible to insert elements in the pipeline for the sole purpose of
forcing a new thread in the pipeline.

There are several reasons to force the use of threads. However, for
performance reasons, you never want to use one thread for every element
out there, since that will create some overhead. Let's now list some
situations where threads can be particularly useful:

  - Data buffering, for example when dealing with network streams or
    when recording data from a live stream such as a video or audio
    card. Short hickups elsewhere in the pipeline will not cause data
    loss. See also [Stream buffering][stream-buffering] about network
    buffering with queue2.

    ![Data buffering, from a networked
    source](images/thread-buffering.png "fig:")

  - Synchronizing output devices, e.g. when playing a stream containing
    both video and audio data. By using threads for both outputs, they
    will run independently and their synchronization will be better.

    ![Synchronizing audio and video
    sinks](images/thread-synchronizing.png "fig:")

Above, we've mentioned the “queue” element several times now. A queue is
the thread boundary element through which you can force the use of
threads. It does so by using a classic provider/consumer model as
learned in threading classes at universities all around the world. By
doing this, it acts both as a means to make data throughput between
threads threadsafe, and it can also act as a buffer. Queues have several
`GObject` properties to be configured for specific uses. For example,
you can set lower and upper thresholds for the element. If there's less
data than the lower threshold (default: disabled), it will block output.
If there's more data than the upper threshold, it will block input or
(if configured to do so) drop data.

To use a queue (and therefore force the use of two distinct threads in
the pipeline), one can simply create a “queue” element and put this in
as part of the pipeline. GStreamer will take care of all threading
details internally.

[stream-buffering]: application-development/advanced/buffering.md#stream-buffering
