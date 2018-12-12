
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

#include "player.h"
#include "quickrenderer.h"

Player::Player(QObject *parent)
    : Player(parent, new QuickRenderer)
{

}

Player::Player(QObject *parent, QuickRenderer *renderer)
    : QGstPlayer::Player(parent, renderer)
    , renderer_(renderer)
{
    renderer_->setParent(this);
}

void Player::setVideoOutput(QVariant output)
{
    QQuickItem *item = qvariant_cast<QQuickItem*>(output);

    renderer_->setVideoItem(item);
}
