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

#ifndef IMAGESAMPLE_H
#define IMAGESAMPLE_H

#include <QObject>
#include <QQuickPaintedItem>
#include <QImage>
#include <QPainter>
#include "player.h"

class ImageSample : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QImage sample READ sample WRITE setSample)
public:
    ImageSample();
    ~ImageSample();
    void paint(QPainter *painter);

    const QImage &sample() const;
    void setSample(const QImage &sample);

private:
    QImage sample_;
};

#endif // IMAGESAMPLE_H
