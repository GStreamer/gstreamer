#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QRunnable>
#include <gst/gst.h>

class SetPlaying : public QRunnable
{
public:
  SetPlaying(GstElement *);
  ~SetPlaying();

  void run ();

private:
  GstElement * pipeline_;
};

SetPlaying::SetPlaying (GstElement * pipeline)
{
  this->pipeline_ = pipeline ? static_cast<GstElement *> (gst_object_ref (pipeline)) : NULL;
}

SetPlaying::~SetPlaying ()
{
  if (this->pipeline_)
    gst_object_unref (this->pipeline_);
}

void
SetPlaying::run ()
{
  if (this->pipeline_)
    gst_element_set_state (this->pipeline_, GST_STATE_PLAYING);
}

int main(int argc, char *argv[])
{
  int ret;

  QGuiApplication app(argc, argv);
  gst_init (&argc, &argv);

  GstElement *pipeline = gst_pipeline_new (NULL);
  GstElement *src = gst_element_factory_make ("videotestsrc", NULL);
  /* the plugin must be loaded before loading the qml file to register the
   * GstGLVideoItem qml item */
  GstElement *sink = gst_element_factory_make ("qmlglsink", NULL);
  GstElement *sinkbin = gst_element_factory_make ("glsinkbin", NULL);

  g_assert (src && sink && sinkbin);

  g_object_set (sinkbin, "sink", sink, NULL);
  g_object_unref (sink);

  gst_bin_add_many (GST_BIN (pipeline), src, sinkbin, NULL);
  gst_element_link_many (src, sinkbin, NULL);

  QQmlApplicationEngine engine;
  engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

  QQuickItem *videoItem;
  QQuickWindow *rootObject;

  /* find and set the videoItem on the sink */
  rootObject = static_cast<QQuickWindow *> (engine.rootObjects().first());
  videoItem = rootObject->findChild<QQuickItem *> ("videoItem");
  g_assert (videoItem);
  g_object_set(sink, "widget", videoItem, NULL);

  rootObject->scheduleRenderJob (new SetPlaying (pipeline),
      QQuickWindow::BeforeSynchronizingStage);

  ret = app.exec();

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  gst_deinit ();

  return ret;
}
