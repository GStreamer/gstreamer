// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (C) 2023, Matthew Waters <matthew@centricular.com>
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

int main(int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);

  {
    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    GstElement *pipeline = gst_pipeline_new (NULL);
    GstElement *src = gst_element_factory_make ("qml6glrendersrc", NULL);
    GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
    GstCaps *caps = gst_caps_from_string ("video/x-raw(ANY),width=640,height=240,framerate=10/1");
    g_object_set (capsfilter, "caps", caps, NULL);
    gst_clear_caps (&caps);
    GstElement *download = gst_element_factory_make ("identity", NULL);
    GstElement *convert = gst_element_factory_make ("identity", NULL);
    GstElement *sink = gst_element_factory_make ("glimagesink", NULL);

    g_assert (src && capsfilter && download && convert && sink);

    gst_bin_add_many (GST_BIN (pipeline), src, capsfilter, download, convert, sink, NULL);
    gst_element_link_many (src, capsfilter, download, convert, sink, NULL);

    /* load qmlglsink output */
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    QQuickItem *rootItem;

    /* find and set the videoItem on the sink */
    rootItem = static_cast<QQuickItem *> (engine.rootObjects().first());
    g_object_set(src, "root-item", rootItem, NULL);

    auto playing = new SetPlaying (pipeline);
    playing->run();
    delete playing;
    //rootObject->scheduleRenderJob (new SetPlaying (pipeline),
    //    QQuickWindow::BeforeSynchronizingStage);

    ret = app.exec();

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  gst_deinit ();

  return ret;
}
