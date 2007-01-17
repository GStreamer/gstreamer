# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

import sys

import gst
import gst.interfaces

pipeline = "alsasrc"
if sys.argv[1:]:
    pipeline = " ".join(sys.argv[1:])
a = gst.element_factory_make(pipeline)
print dir(a)

res = a.set_state(gst.STATE_PAUSED)
if res != gst.STATE_CHANGE_SUCCESS:
    print "Could not set pipeline %s to PAUSED" % pipeline

print "Inputs:"
for t in a.list_tracks():
    if t.flags & gst.interfaces.MIXER_TRACK_INPUT:
        sys.stdout.write(t.label)
    sys.stdout.write(': %d - %d' % (t.min_volume, t.max_volume))
    volumes = a.get_volume(t)
    sys.stdout.write(': %r' % (volumes, ))
    if t.props.num_channels > 0:
        a.set_volume(t, volumes=volumes)
    if t.flags & gst.interfaces.MIXER_TRACK_RECORD:
        sys.stdout.write(' (selected)')
    sys.stdout.write('\n')

