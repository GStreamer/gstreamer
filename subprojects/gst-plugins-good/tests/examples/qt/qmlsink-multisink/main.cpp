// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (C) 2015, Matthew Waters <matthew@centricular.com>
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
#include <QQmlContext>

#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  int ret;
  {
    QGuiApplication app (argc, argv);
    QQmlApplicationEngine engine;

    /* make sure that plugin was loaded */
    GstElement *qmlglsink = gst_element_factory_make ("qmlglsink", NULL);
    g_assert (qmlglsink);
    gst_clear_object (&qmlglsink);

    /* anything supported by videotestsrc */
    QStringList patterns (
        {
        "smpte", "ball", "spokes", "gamut"});

    engine.rootContext ()->setContextProperty ("patterns",
        QVariant::fromValue (patterns));

    QObject::connect (&engine, &QQmlEngine::quit, [&] {
          qApp->quit ();
        });

    engine.load (QUrl (QStringLiteral ("qrc:///main.qml")));

    ret = app.exec();
  }
  gst_deinit();

  return ret;
}
