#include <stdlib.h>
#include <gst/gst.h>

#include "testrtpool.h"

static GstTaskPool *pool;

static void
event_loop (GstBus * bus, GstElement * pipe)
{
  GstMessage *message = NULL;

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    g_assert (message != NULL);

    switch (message->type) {
      case GST_MESSAGE_EOS:
        g_message ("received EOS");
        gst_message_unref (message);
        return;
      case GST_MESSAGE_WARNING:
      {
        GError *gerror;
        gchar *debug;

        gst_message_parse_warning (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        g_error_free (gerror);
        g_free (debug);
        return;
      }
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        g_error_free (gerror);
        g_free (debug);
        return;
      }
      default:
        gst_message_unref (message);
        break;
    }
  }
}

static GstBusSyncReply
sync_bus_handler (GstBus * bus, GstMessage * message, GstElement * bin)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STREAM_STATUS:
    {
      GstStreamStatusType type;
      GstElement *owner;
      const GValue *val;
      gchar *path;
      GstTask *task = NULL;

      g_message ("received STREAM_STATUS");
      gst_message_parse_stream_status (message, &type, &owner);

      val = gst_message_get_stream_status_object (message);

      g_message ("type:   %d", type);
      path = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      g_message ("source: %s", path);
      g_free (path);
      path = gst_object_get_path_string (GST_OBJECT (owner));
      g_message ("owner:  %s", path);
      g_free (path);

      if (G_VALUE_HOLDS_OBJECT (val)) {
        g_message ("object: type %s, value %p", G_VALUE_TYPE_NAME (val),
            g_value_get_object (val));
      } else if (G_VALUE_HOLDS_POINTER (val)) {
        g_message ("object: type %s, value %p", G_VALUE_TYPE_NAME (val),
            g_value_get_pointer (val));
      } else if (G_IS_VALUE (val)) {
        g_message ("object: type %s", G_VALUE_TYPE_NAME (val));
      } else {
        g_message ("object: (null)");
        break;
      }

      /* see if we know how to deal with this object */
      if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
        task = g_value_get_object (val);
      }

      switch (type) {
        case GST_STREAM_STATUS_TYPE_CREATE:
          if (task) {
            g_message ("created task %p, setting pool", task);
            gst_task_set_pool (task, pool);
          }
          break;
        case GST_STREAM_STATUS_TYPE_ENTER:
          break;
        case GST_STREAM_STATUS_TYPE_LEAVE:
          break;
        default:
          break;
      }
      break;
    }
    default:
      break;
  }
  /* pass all messages on the async queue */
  return GST_BUS_PASS;
}

int
main (int argc, char *argv[])
{
  GstElement *bin, *alsasrc, *alsasink;
  GstBus *bus;
  GstStateChangeReturn ret;

  gst_init (&argc, &argv);

  /* create a custom thread pool */
  pool = test_rt_pool_new ();

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  /* create a source */
  alsasrc = gst_element_factory_make ("alsasrc", "alsasrc");
  g_assert (alsasrc);
  g_object_set (alsasrc, "device", "hw:0", NULL);
  g_object_set (alsasrc, "latency-time", (gint64) 2000, NULL);
  g_object_set (alsasrc, "slave-method", 2, NULL);

  /* and a sink */
  alsasink = gst_element_factory_make ("alsasink", "alsasink");
  g_assert (alsasink);
  g_object_set (alsasink, "device", "hw:0", NULL);
  g_object_set (alsasink, "latency-time", (gint64) 2000, NULL);
  g_object_set (alsasink, "buffer-time", (gint64) 10000, NULL);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), alsasrc, alsasink, NULL);

  /* link the elements */
  gst_element_link (alsasrc, alsasink);

  /* get the bus, we need to install a sync handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (bin));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) sync_bus_handler, bin);

  /* start playing */
  ret = gst_element_set_state (bin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return 0;

  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (bus, bin);

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bus);

  exit (0);
}
