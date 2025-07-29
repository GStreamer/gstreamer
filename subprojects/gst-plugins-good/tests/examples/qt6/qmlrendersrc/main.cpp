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
#include <QDirIterator>
#include <QTimer>
#include <gst/gst.h>

class SwitchFramerate : public QTimer {
public:
  SwitchFramerate(GstElement *capsfilter) : QTimer() {
    m_capsfilter = (GstElement *) gst_object_ref ((gpointer) capsfilter);
    QObject::connect(this, &QTimer::timeout, this, QOverload<>::of(&SwitchFramerate::switchFramerate));
    m_currentFramerate = 10;
  }
  ~SwitchFramerate() {
    gst_object_unref (m_capsfilter);
  }

  void switchFramerate() {
    GstCaps* caps;
    g_object_get (m_capsfilter, "caps", &caps, NULL);
    if (!caps)
      return;

    caps = gst_caps_make_writable (caps);
    if (m_currentFramerate <= 10) {
      m_currentFramerate = 20;
    } else {
      m_currentFramerate = 10;
    }
    gst_println ("changing framerate to %u", m_currentFramerate);
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, m_currentFramerate, 1, NULL);
    g_object_set (m_capsfilter, "caps", caps, NULL);
    gst_clear_caps (&caps);
  }

  GstElement *m_capsfilter;
  int m_currentFramerate;
};

int main(int argc, char *argv[])
{
  int ret;
  bool do_switch = false;

  gst_init (&argc, &argv);

  if (argc >= 2 && g_strcmp0(argv[1], "switch") == 0)
    do_switch = true;

  {
    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    GstElement *pipeline = gst_pipeline_new (NULL);
    GstElement *src = gst_element_factory_make ("qml6glrendersrc", NULL);
    gst_util_set_object_arg (G_OBJECT (src), "max-framerate", "10/1");
    GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
    GstCaps *caps;
    if (do_switch)
      caps = gst_caps_from_string ("video/x-raw(ANY),width=640,height=240,framerate=20/1");
    else
      caps = gst_caps_from_string ("video/x-raw(ANY),width=640,height=240,framerate=0/1");
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

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    SwitchFramerate *timer = nullptr;
    if (do_switch) {
      SwitchFramerate *timer = new SwitchFramerate(capsfilter);
      timer->start(2000);
    }

    ret = app.exec();

    if (do_switch) {
      delete timer;
    }

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  gst_deinit ();

  return ret;
}
