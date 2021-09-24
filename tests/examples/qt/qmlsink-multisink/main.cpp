#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  QGuiApplication app (argc, argv);
  QQmlApplicationEngine engine;

  /* make sure that plugin was loaded */
  GstElement *qmlglsink = gst_element_factory_make ("qmlglsink", NULL);
  g_assert (qmlglsink);

  /* anything supported by videotestsrc */
  QStringList patterns (
      {
      "smpte", "ball", "spokes", "gamut"});

  engine.rootContext ()->setContextProperty ("patterns",
      QVariant::fromValue (patterns));

  QObject::connect (&engine, &QQmlEngine::quit, [&] {
        gst_object_unref (qmlglsink);
        qApp->quit ();
      });

  engine.load (QUrl (QStringLiteral ("qrc:///main.qml")));

  return app.exec ();
}
