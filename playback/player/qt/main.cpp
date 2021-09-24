/* GStreamer
 *
 * Copyright (C) 2015 Alexandre Moreno <alexmorenocano@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QCommandLineParser>
#include <QStringList>
#include <QUrl>
#include <gst/gst.h>

int main(int argc, char *argv[])
{
    /* Use QApplication instead of QGuiApplication since the latter is needed
     * for widgets like the QFileDialog to work */
    QApplication app(argc, argv);
    int result;

    QCommandLineParser parser;
    parser.setApplicationDescription("GstPlayer");
    parser.addHelpOption();
    parser.addPositionalArgument("urls",
        QCoreApplication::translate("main", "URLs to play, optionally."), "[urls...]");
    parser.process(app);

    QList<QUrl> media_files;

    const QStringList args = parser.positionalArguments();
    foreach (const QString file, args) {
        media_files << QUrl::fromUserInput(file);
    }

    /* the plugin must be loaded before loading the qml file to register the
     * GstGLVideoItem qml item
     * FIXME Add a QQmlExtensionPlugin into qmlglsink to register GstGLVideoItem
     * with the QML engine, then remove this */
    gst_init(NULL,NULL);
    GstElement *sink = gst_element_factory_make ("qmlglsink", NULL);
    gst_object_unref(sink);


    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    QObject *rootObject = engine.rootObjects().first();

    QObject *player = rootObject->findChild<QObject*>("player");
    QObject *videoItem = rootObject->findChild<QObject*>("videoItem");
    QVariant v;
    v.setValue<QObject*>(videoItem);
    QQmlProperty(player, "videoOutput").write(v);

    if (!media_files.isEmpty()) {
        QVariant v;
        v.setValue<QList<QUrl>>(media_files);
        QQmlProperty(player, "playlist").write(QVariant (v));
    }

    result = app.exec();

    gst_deinit ();
    return result;
}
