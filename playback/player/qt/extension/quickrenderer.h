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

#ifndef QUICKPLAYER_H
#define QUICKPLAYER_H

#include <QObject>
#include <QQuickItem>
#include "qgstplayer.h"

class QuickRenderer : public QObject, public QGstPlayer::VideoRenderer
{
    Q_OBJECT
public:
    QuickRenderer(QObject *parent = 0);
    ~QuickRenderer();

    GstElement *createVideoSink();
    void setVideoItem(QQuickItem *item);

private:
    GstElement *sink;
};

#endif // QUICKPLAYER_H
