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

#include "player.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<Player>("Player", 1, 0, "Player");

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

    Player *player = rootObject->findChild<Player*>("player");
    QQuickItem *videoItem = rootObject->findChild<QQuickItem*>("videoItem");
    player->setVideoOutput(videoItem);


    return app.exec();
}
