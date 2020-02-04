#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QRunnable>
#include <QTimer>
#include <gst/gst.h>
#include <gst/gl/gl.h>

static GstBusSyncReply
on_sync_bus_message (GstBus * bus, GstMessage * msg, gpointer data)
{
  GstElement *pipeline = (GstElement *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_HAVE_CONTEXT: {
      GstContext *context;

      gst_message_parse_have_context (msg, &context);

      /* if you need specific behviour or a context from a specific element,
       * you need to be selective about which context's you set on the
       * pipeline */
      if (gst_context_has_context_type (context, GST_GL_DISPLAY_CONTEXT_TYPE)) {
        gst_println ("got have-context %p", context);
        gst_element_set_context (pipeline, context);
      }

      if (context)
        gst_context_unref (context);
      gst_message_unref (msg);
      return GST_BUS_DROP;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static void
connect_tee (GstElement * tee, GstElement * queue)
{
  gst_println ("attaching tee/queue %p %p", tee, queue);
  gst_element_link (tee, queue);
}

static void
connect_qmlglsink (GstElement * pipeline, GstElement * tee, QQuickWindow * rootObject)
{
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *qmlglsink = gst_element_factory_make ("qmlglsink", NULL);
  QQuickItem *videoItem;

  gst_println ("attaching qmlglsink %s at %p", GST_OBJECT_NAME (qmlglsink), qmlglsink);

  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (pipeline), qmlglsink);
  gst_element_link (queue, qmlglsink);
  gst_element_set_state (queue, GST_STATE_PLAYING);

  videoItem = rootObject->findChild<QQuickItem *> ("videoItem");
  g_assert (videoItem);
  g_object_set (qmlglsink, "widget", videoItem, NULL);

  gst_element_set_state (qmlglsink, GST_STATE_PAUSED);
  connect_tee (tee, queue);
  gst_element_set_state (qmlglsink, GST_STATE_PLAYING);
}

int main(int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);

  {
    QGuiApplication app(argc, argv);

    /* test a whole bunch of elements respect the change in display
     * and therefore OpenGL context */
    GstElement *pipeline = gst_parse_launch ("gltestsrc ! "
        "capsfilter caps=video/x-raw(ANY),framerate=10/1 ! glupload ! "
        "glcolorconvert ! glalpha noise-level=16 method=green angle=40 ! "
        "glcolorbalance hue=0.25 ! gltransformation rotation-x=30 ! "
        "glvideomixerelement ! glviewconvert output-mode-override=side-by-side ! "
        "glstereosplit name=s "
        "glstereomix name=m ! tee name=t ! queue ! fakesink sync=true "
        "s.left ! queue ! m.sink_0 "
        "s.right ! queue ! m.sink_1", NULL);
    GstBus *bus = gst_element_get_bus (pipeline);
    gst_bus_set_sync_handler (bus, on_sync_bus_message, pipeline, NULL);
    gst_object_unref (bus);
    /* the plugin must be loaded before loading the qml file to register the
     * GstGLVideoItem qml item */
    GstElement *sink = gst_element_factory_make ("qmlglsink", NULL);

    g_assert (pipeline && sink);
    gst_object_unref (sink);

    QQuickWindow *rootObject;

    /* The Qml scene starts out with the widget not connected to any qmlglsink
     * element */
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    /* find and set the videoItem on the sink */
    rootObject = static_cast<QQuickWindow *> (engine.rootObjects().first());

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    GstElement *t = gst_bin_get_by_name (GST_BIN (pipeline), "t");
    gst_object_unref (t);  /* ref held by pipeline */
    /* add the qmlglsink element */
    QTimer::singleShot(5000, [pipeline, t, rootObject]() { connect_qmlglsink (pipeline, t, rootObject); } );

    ret = app.exec();

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  gst_deinit ();

  return ret;
}
