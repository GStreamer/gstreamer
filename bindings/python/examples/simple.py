#!/usr/bin/env python
#
#       simple.py
#
# Copyright (C) 2011 Thibault Saunier <thibault.saunier@collabora.co.uk>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.

from gst import ges

import sys
import getopt
import gst
import glib

class Demo:
    def __init__(self, files):
        self.tl_objs = []
        self.set_pipeline (files)

        for f in files:
            self.add_tl_object(f)
            self.mainloop = glib.MainLoop()

    def add_tl_object(self, file):
        self.tl_objs.append(ges.TimelineFileSource (file))
        self.layer.add_object(self.tl_objs[-1], -1)
        transition = ges.TimelineStandardTransition("crossfade")
        transition.duration = gst.SECOND * 2
        self.layer.add_object(transition, -1)

    def bus_handler(self, unused_bus, message):
        if message.type == gst.MESSAGE_ERROR:
            print "ERROR"
            self.mainloop.quit()
        elif message.type == gst.MESSAGE_EOS:
            print "Done"
            self.mainloop.quit()

        return gst.BUS_PASS

    def set_pipeline (self, files):
        try:
            opts, args = getopt.getopt(sys.argv[1:], "h", ["help"])
        except getopt.error, msg:
            print msg
            print "for help use --help"

        ges.init()

        tl = ges.timeline_new_audio_video()
        layer = ges.SimpleTimelineLayer()
        tl.add_layer(layer)

        pipeline = ges.TimelinePipeline()
        pipeline.add_timeline (tl)
        bus = pipeline.get_bus()
        bus.set_sync_handler(self.bus_handler)

        self.pipeline = pipeline
        self.layer = layer

    def run(self):
        if (self.pipeline.set_state(gst.STATE_PLAYING) == gst.STATE_CHANGE_FAILURE):
            print "Couldn't start pipeline"

        self.mainloop.run()


def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "h", ["help"])
    except getopt.error, msg:
        print msg
        print "for help use --help"
        sys.exit(2)
        # process options
    for o, a in opts:
        if o in ("-h", "--help"):
            print __doc__
            sys.exit(0)

    demo = Demo(args)
    demo.run()


if __name__ == "__main__":
    main()
