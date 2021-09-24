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

#include "quickrenderer.h"

QuickRenderer::QuickRenderer(QObject *parent)
    : QObject(parent)
    , QGstPlayer::VideoRenderer()
    , sink()
{

}

QuickRenderer::~QuickRenderer()
{
    if (sink) gst_object_unref(sink);
}

GstElement *QuickRenderer::createVideoSink()
{
    GstElement *qmlglsink = gst_element_factory_make("qmlglsink", NULL);

    GstElement *glsinkbin = gst_element_factory_make ("glsinkbin", NULL);

    Q_ASSERT(qmlglsink && glsinkbin);

    g_object_set (glsinkbin, "sink", qmlglsink, NULL);

    sink = static_cast<GstElement*>(gst_object_ref_sink(qmlglsink));

    return glsinkbin;
}

void QuickRenderer::setVideoItem(QQuickItem *item)
{
    Q_ASSERT(item);

    g_object_set(sink, "widget", item, NULL);
}
