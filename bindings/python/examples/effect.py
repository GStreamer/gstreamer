#!/usr/bin/env python
#
#       effect.py
#
# Copyright (C) 2011 Mathieu Duponchelle <seeed@laposte.net>
# Copyright (C) 2011 Luis de Bethencourt <luis.debethencourt@collabora.co.uk>
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

import sys
import optparse

import glib
import gobject
gobject.threads_init()

import gst
import ges

class Effect:
    def __init__(self, effects):
        ges.init()
        self.mainloop = glib.MainLoop()

        self.timeline = ges.timeline_new_audio_video()
        layer = ges.TimelineLayer()
        self.src = ges.TimelineTestSource()
        self.src.set_start(long(0))

        self.src.set_duration(long(3000000000))
        self.src.set_vpattern("smpte75")
        layer.add_object(self.src)
        self.timeline.add_layer(layer)

        self.add_effects(effects)

        self.pipeline = ges.TimelinePipeline()
        self.pipeline.add_timeline(self.timeline)
        bus = self.pipeline.get_bus()
        bus.set_sync_handler(self.bus_handler)

    def add_effects(self, effects):
        for e in effects:
            effect = ges.TrackParseLaunchEffect(e)
            self.src.add_track_object(effect)
            for track in self.timeline.get_tracks():
                if track.get_caps().to_string() == \
                        "video/x-raw-yuv; video/x-raw-rgb":
                    print "setting effect: " + e
                    track.add_object(effect)

    def bus_handler(self, unused_bus, message):
        if message.type == gst.MESSAGE_ERROR:
            print "ERROR"
            self.mainloop.quit()
        elif message.type == gst.MESSAGE_EOS:
            print "Done"
            self.mainloop.quit()

        return gst.BUS_PASS

    def run(self):
        if (self.pipeline.set_state(gst.STATE_PLAYING) == \
                gst.STATE_CHANGE_FAILURE):
            print "Couldn't start pipeline"

        self.mainloop.run()

def main(args):
    usage = "usage: %s effect_name-1 .. effect_name-n\n" % args[0]

    if len(args) < 2:
        print usage + "using aging tv as a default instead"
        args.append("agingtv")

    parser = optparse.OptionParser (usage=usage)
    (opts, args) = parser.parse_args ()

    effect = Effect(args)
    effect.run()

if __name__ == "__main__":
    main(sys.argv)
