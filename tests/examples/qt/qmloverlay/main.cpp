#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QRunnable>
#include <QDirIterator>
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

static void
on_overlay_scene_initialized (GstElement * overlay, gpointer root_item, gpointer unused)
{
  GST_INFO ("scene initialized");
  QQuickItem *rootObject = static_cast<QQuickItem *> (root_item);
  QQuickItem *videoItem = rootObject->findChild<QQuickItem *> ("inputVideoItem");
  g_object_set (overlay, "widget", videoItem, NULL);
}

int main(int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);

  {
    QGuiApplication app(argc, argv);

    GstElement *pipeline = gst_pipeline_new (NULL);
    GstElement *src = gst_element_factory_make ("videotestsrc", NULL);
    GstElement *glupload = gst_element_factory_make ("glupload", NULL);
    /* the plugin must be loaded before loading the qml file to register the
     * GstGLVideoItem qml item */
    GstElement *overlay = gst_element_factory_make ("qmlgloverlay", NULL);
    GstElement *sink = gst_element_factory_make ("qmlglsink", NULL);

    g_assert (src && glupload && overlay && sink);

    gst_bin_add_many (GST_BIN (pipeline), src, glupload, overlay, sink, NULL);
    gst_element_link_many (src, glupload, overlay, sink, NULL);

    /* load qmlglsink output */
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    QQuickItem *videoItem;
    QQuickWindow *rootObject;

    /* find and set the videoItem on the sink */
    rootObject = static_cast<QQuickWindow *> (engine.rootObjects().first());
    videoItem = rootObject->findChild<QQuickItem *> ("videoItem");
    g_assert (videoItem);
    g_object_set(sink, "widget", videoItem, NULL);

    QDirIterator it(":", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qDebug() << it.next();
    }

    QFile f(":/overlay.qml");
    if(!f.open(QIODevice::ReadOnly)) {
        qWarning() << "error: " << f.errorString();
        return 1;
    }
    QByteArray overlay_scene = f.readAll();
    qDebug() << overlay_scene;

    /* load qmlgloverlay contents */
    g_signal_connect (overlay, "qml-scene-initialized", G_CALLBACK (on_overlay_scene_initialized), NULL);
    g_object_set (overlay, "qml-scene", overlay_scene.data(), NULL);

    rootObject->scheduleRenderJob (new SetPlaying (pipeline),
        QQuickWindow::BeforeSynchronizingStage);

    ret = app.exec();

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  gst_deinit ();

  return ret;
}
