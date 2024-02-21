#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  int ret;
  {
    QGuiApplication app (argc, argv);
    QQmlApplicationEngine engine;

    /* make sure that plugin was loaded */
    GstElement *qmlglsink = gst_element_factory_make ("qmlglsink", NULL);
    g_assert (qmlglsink);
    gst_clear_object (&qmlglsink);

    /* anything supported by videotestsrc */
    QStringList patterns (
        {
        "smpte", "ball", "spokes", "gamut"});

    engine.rootContext ()->setContextProperty ("patterns",
        QVariant::fromValue (patterns));

    QObject::connect (&engine, &QQmlEngine::quit, [&] {
          qApp->quit ();
        });

    engine.load (QUrl (QStringLiteral ("qrc:///main.qml")));

    ret = app.exec();
  }
  gst_deinit();

  return ret;
}
