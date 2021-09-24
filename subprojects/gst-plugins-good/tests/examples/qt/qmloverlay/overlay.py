#!/usr/bin/env python

import signal
signal.signal(signal.SIGINT, signal.SIG_DFL)

import sys
import gi
gi.require_version('Gst', '1.0')

from gi.repository import Gst, GLib
from PySide2.QtGui import QGuiApplication
from PySide2.QtQuick import QQuickItem

def main(args):
    app = QGuiApplication(args)
    Gst.init(args)

    pipeline = Gst.parse_launch("""videotestsrc ! glupload ! qmlgloverlay name=o ! gldownload ! videoconvert ! autovideosink""")
    o = pipeline.get_by_name('o')
    f = open('overlay.qml', 'r')
    o.set_property('qml-scene', f.read())

    pipeline.set_state(Gst.State.PLAYING)
    app.exec_()
    pipeline.set_state(Gst.State.NULL)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
