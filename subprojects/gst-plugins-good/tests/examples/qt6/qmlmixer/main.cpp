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
on_mixer_scene_initialized (GstElement * mixer, gpointer unused)
{
  QQuickItem *rootObject;
  GST_INFO ("scene initialized");
  g_object_get (mixer, "root-item", &rootObject, NULL);

  QQuickItem *videoItem0 = rootObject->findChild<QQuickItem *> ("inputVideoItem0");
  GstPad *sink0 = gst_element_get_static_pad (mixer, "sink_0");
  g_object_set (sink0, "widget", videoItem0, NULL);
  gst_clear_object (&sink0);

  QQuickItem *videoItem1 = rootObject->findChild<QQuickItem *> ("inputVideoItem1");
  GstPad *sink1 = gst_element_get_static_pad (mixer, "sink_1");
  g_object_set (sink1, "widget", videoItem1, NULL);
  gst_clear_object (&sink1);
}

int main(int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);

  {
    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    GstElement *pipeline = gst_pipeline_new (NULL);
    GstElement *src0 = gst_element_factory_make ("videotestsrc", NULL);
    GstElement *glupload0 = gst_element_factory_make ("glupload", NULL);
    GstElement *src1 = gst_element_factory_make ("videotestsrc", NULL);
    gst_util_set_object_arg ((GObject *) src1, "pattern", "ball");
    GstElement *glupload1 = gst_element_factory_make ("glupload", NULL);
    /* the plugin must be loaded before loading the qml file to register the
     * GstGLVideoItem qml item */
    GstElement *mixer = gst_element_factory_make ("qml6glmixer", NULL);
    GstElement *sink = gst_element_factory_make ("qml6glsink", NULL);

    g_assert (src0 && glupload0 && mixer && sink);

    gst_bin_add_many (GST_BIN (pipeline), src0, glupload0, src1, glupload1, mixer, sink, NULL);
    gst_element_link_many (src0, glupload0, mixer, sink, NULL);
    gst_element_link_many (src1, glupload1, mixer, NULL);

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

    QFile f(":/mixer.qml");
    if(!f.open(QIODevice::ReadOnly)) {
        qWarning() << "error: " << f.errorString();
        return 1;
    }
    QByteArray overlay_scene = f.readAll();
    qDebug() << overlay_scene;

    /* load qmlgloverlay contents */
    g_signal_connect (mixer, "qml-scene-initialized", G_CALLBACK (on_mixer_scene_initialized), NULL);
    g_object_set (mixer, "qml-scene", overlay_scene.data(), NULL);

    rootObject->scheduleRenderJob (new SetPlaying (pipeline),
        QQuickWindow::BeforeSynchronizingStage);

    ret = app.exec();

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  gst_deinit ();

  return ret;
}
