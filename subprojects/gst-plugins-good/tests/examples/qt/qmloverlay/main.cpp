// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (C) 2020, Matthew Waters <matthew@centricular.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// a) Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// b) Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QRunnable>
#include <QDirIterator>
#include <gst/gst.h>

class SetPlaying : public QRunnable
{
public:
  SetPlaying(GstElement *);
  ~SetPlaying();

  void run ();

private:
  GstElement * pipeline_;
};

SetPlaying::SetPlaying (GstElement * pipeline)
{
  this->pipeline_ = pipeline ? static_cast<GstElement *> (gst_object_ref (pipeline)) : NULL;
}

SetPlaying::~SetPlaying ()
{
  if (this->pipeline_)
    gst_object_unref (this->pipeline_);
}

void
SetPlaying::run ()
{
  if (this->pipeline_)
    gst_element_set_state (this->pipeline_, GST_STATE_PLAYING);
}

static void
on_overlay_scene_initialized (GstElement * overlay, gpointer unused)
{
  QQuickItem *rootObject;
  GST_INFO ("scene initialized");
  g_object_get (overlay, "root-item", &rootObject, NULL);
  QQuickItem *videoItem = rootObject->findChild<QQuickItem *> ("inputVideoItem");
  g_object_set (overlay, "widget", videoItem, NULL);
}

int main(int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);

  {
    QGuiApplication app(argc, argv);

    GstElement *pipeline = gst_pipeline_new (NULL);
    GstElement *src = gst_element_factory_make ("videotestsrc", NULL);
    GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
    GstCaps *caps = gst_caps_from_string ("video/x-raw,format=RGBA");
    g_object_set (capsfilter, "caps", caps, NULL);
    gst_caps_unref (caps);
    GstElement *glupload = gst_element_factory_make ("glupload", NULL);
    /* the plugin must be loaded before loading the qml file to register the
     * GstGLVideoItem qml item */
    GstElement *overlay = gst_element_factory_make ("qmlgloverlay", NULL);
    GstElement *overlay2 = gst_element_factory_make ("qmlgloverlay", NULL);
    GstElement *sink = gst_element_factory_make ("qmlglsink", NULL);

    g_assert (src && glupload && overlay && sink);

    gst_bin_add_many (GST_BIN (pipeline), src, capsfilter, glupload, overlay, overlay2, sink, NULL);
    gst_element_link_many (src, capsfilter, glupload, overlay, overlay2, sink, NULL);

    /* load qmlglsink output */
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    QQuickItem *videoItem;
    QQuickWindow *rootObject;

    /* find and set the videoItem on the sink */
    rootObject = static_cast<QQuickWindow *> (engine.rootObjects().first());
    videoItem = rootObject->findChild<QQuickItem *> ("videoItem");
    g_assert (videoItem);
    g_object_set(sink, "widget", videoItem, NULL);

    QDirIterator it(":", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qDebug() << it.next();
    }

    QFile f(":/overlay.qml");
    if(!f.open(QIODevice::ReadOnly)) {
        qWarning() << "error: " << f.errorString();
        return 1;
    }
    QByteArray overlay_scene = f.readAll();
    qDebug() << overlay_scene;

    QFile f2(":/overlay2.qml");
    if(!f2.open(QIODevice::ReadOnly)) {
        qWarning() << "error: " << f2.errorString();
        return 1;
    }
    QByteArray overlay_scene2 = f2.readAll();
    qDebug() << overlay_scene2;

    /* load qmlgloverlay contents */
    g_signal_connect (overlay, "qml-scene-initialized", G_CALLBACK (on_overlay_scene_initialized), NULL);
    g_object_set (overlay, "qml-scene", overlay_scene.data(), NULL);

    g_signal_connect (overlay2, "qml-scene-initialized", G_CALLBACK (on_overlay_scene_initialized), NULL);
    g_object_set (overlay2, "qml-scene", overlay_scene2.data(), NULL);

    rootObject->scheduleRenderJob (new SetPlaying (pipeline),
        QQuickWindow::BeforeSynchronizingStage);

    ret = app.exec();

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  gst_deinit ();

  return ret;
}
