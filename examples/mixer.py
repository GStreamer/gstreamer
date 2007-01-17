# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

import sys

import gst
import gst.interfaces

a = gst.element_factory_make('alsasrc')
print a.set_state(gst.STATE_PLAYING)

print "Inputs:"
for t in a.list_tracks():
    if t.flags & gst.interfaces.MIXER_TRACK_INPUT:
        sys.stdout.write(t.label)
    sys.stdout.write(': %d - %d' % (t.min_volume, t.max_volume))
    sys.stdout.write(': %r' % (a.get_volume(t), ))
    if t.flags & gst.interfaces.MIXER_TRACK_RECORD:
        sys.stdout.write(' (selected)')
    sys.stdout.write('\n')
