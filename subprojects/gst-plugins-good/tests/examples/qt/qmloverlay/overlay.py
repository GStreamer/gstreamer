#!/usr/bin/env python
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (C) 2021, The Qt Company Ltd.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# a) Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# b) Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

from PySide2.QtQuick import QQuickItem
from PySide2.QtGui import QGuiApplication
from gi.repository import Gst, GLib
import gi
import sys
import signal
signal.signal(signal.SIGINT, signal.SIG_DFL)

gi.require_version('Gst', '1.0')


def main(args):
    app = QGuiApplication(args)
    Gst.init(args)

    pipeline = Gst.parse_launch(
        """videotestsrc ! glupload ! qmlgloverlay name=o ! gldownload ! videoconvert ! autovideosink""")
    o = pipeline.get_by_name('o')
    f = open('overlay.qml', 'r')
    o.set_property('qml-scene', f.read())

    pipeline.set_state(Gst.State.PLAYING)
    app.exec_()
    pipeline.set_state(Gst.State.NULL)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
