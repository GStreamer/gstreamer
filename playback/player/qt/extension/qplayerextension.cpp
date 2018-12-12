/* GStreamer
 *
 * Copyright (C) 2018 Matthew Waters <matthew@cenricular.com>
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

#include "qplayerextension.h"
#include "qgstplayer.h"
#include "imagesample.h"

#include <iostream>

void QGstPlayerPlayerExtension::registerTypes(const char *uri)
{
        Q_ASSERT(uri == QLatin1String("extension"));
        std::cout << "register uri: " << uri << std::endl;
        qmlRegisterType<Player>(uri, 1, 0, "Player");
        qmlRegisterType<ImageSample>("extension", 1, 0, "ImageSample");
}
