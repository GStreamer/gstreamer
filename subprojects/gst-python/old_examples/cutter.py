#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# gst-python
# Copyright (C) 2005 Thomas Vander Stichele
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.

import gst
import time

import gobject
#gobject.threads_init() # so we can safely receive signals from threads

count = 0

def on_message_application(cutter, message, loop):
    global count
    s = message.structure
    which = 'below'
    if s['above']: which = 'above'
    print "%s: %s threshold" % (gst.TIME_ARGS(s['timestamp']), which)
    if s['above']: count += 1
    if count > 2: loop.quit()

def main():
    type = 'async'
    loop = gobject.MainLoop()

    pipeline = gst.Pipeline("cutter")
    src = gst.element_factory_make("sinesrc", "src")
    cutter = gst.element_factory_make("cutter")
    cutter.set_property('threshold', 0.5)
    sink = gst.element_factory_make("fakesink", "sink")
    pipeline.add(src, cutter, sink)
    src.link(cutter)
    cutter.link(sink)

    control = gst.Controller(src, "volume")
    control.set_interpolation_mode("volume", gst.INTERPOLATE_LINEAR)

    control.set("volume", 0, 0.0)
    control.set("volume", 2 * gst.SECOND, 1.0)
    control.set("volume", 4 * gst.SECOND, 0.0)
    control.set("volume", 6 * gst.SECOND, 1.0)
    control.set("volume", 8 * gst.SECOND, 0.0)
    control.set("volume", 10 * gst.SECOND, 1.0)

    bus = pipeline.get_bus()

    if type == 'async':
        bus.add_signal_watch()
        bus.connect('message::element', on_message_application, loop)
    else:
        # FIXME: needs wrapping in gst-python
        bus.set_sync_handler(bus.sync_signal_handler)
        bus.connect('sync-message::element', on_message_application, loop)

    pipeline.set_state(gst.STATE_PLAYING)

    loop.run()

    pipeline.set_state(gst.STATE_NULL)

if __name__ == "__main__":
    main()
