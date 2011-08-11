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

import sys
import optparse

import glib
import gobject
gobject.threads_init()

import gst
import ges

class Simple:
    def __init__(self, uris):
        # init ges to have debug logs
        ges.init()
        self.mainloop = glib.MainLoop()

        timeline = ges.timeline_new_audio_video()
        self.layer = ges.SimpleTimelineLayer()
        timeline.add_layer(self.layer)

        self.pipeline = ges.TimelinePipeline()
        self.pipeline.add_timeline(timeline)
        bus = self.pipeline.get_bus()
        bus.set_sync_handler(self.bus_handler)

        # all timeline objects except the last will have a transition at the end
        for n in uris[:-1]:
            self.add_timeline_object(n, True)
        self.add_timeline_object(uris[-1], False)

    def add_timeline_object(self, uri, do_transition):
        filesource = ges.TimelineFileSource (uri)
        filesource.set_duration(long (gst.SECOND * 5))
        self.layer.add_object(filesource, -1)

        if do_transition:
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

    def run(self):
        if (self.pipeline.set_state(gst.STATE_PLAYING) == \
                gst.STATE_CHANGE_FAILURE):
            print "Couldn't start pipeline"

        self.mainloop.run()


def main(args):
    usage = "usage: %s URI-OF-VIDEO-1 ... URI-OF-VIDEO-n\n" % args[0]

    if len(args) < 2:
        sys.stderr.write(usage)
        sys.exit(1)

    parser = optparse.OptionParser (usage=usage)
    (options, args) = parser.parse_args ()

    simple = Simple(args)
    simple.run()

if __name__ == "__main__":
    sys.exit(main(sys.argv))
