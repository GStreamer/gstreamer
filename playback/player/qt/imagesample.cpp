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

#include <QPainter>
#include "imagesample.h"

ImageSample::ImageSample()
    : QQuickPaintedItem()
    , sample_()
{

}

ImageSample::~ImageSample()
{

}

void ImageSample::paint(QPainter *painter)
{
    if (sample_.size().isEmpty())
        return;

    float aspect_ratio = sample_.width() / sample_.height();
    int w  = height() * aspect_ratio;
    int x = (width() - w) / 2;

    painter->setViewport(x, 0, w, height());
    painter->drawImage(QRectF(0, 0, width(), height()), sample_);
}

const QImage &ImageSample::sample() const
{
    return sample_;
}

void ImageSample::setSample(const QImage &sample)
{
    sample_ = sample;
    update();
}



