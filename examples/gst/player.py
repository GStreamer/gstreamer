#!/usr/bin/env python
import os
import sys

import gst

def found_tags(element, source, tags):
    for tag in tags.keys():
        print "%s: %s" % (gst.tag_get_nick(tag), tags[tag])

#def error(source, error, debug):

def deep_notify(*args):
    pass

def error(*args):
    print args

def playfile(filename):
    bin = gst.Pipeline('player')
    bin.connect('deep-notify', deep_notify)
    bin.connect('error', error)
    
    source = gst.Element('filesrc', 'src')
    source.set_property('location', filename)

    spider = gst.Element('spider', 'spider')
    spider.connect('found-tag', found_tags)
    
    sink = gst.Element('osssink', 'sink')
    #sink.set_property('release-device', 1)

    bin.add_many(source, spider, sink)
    if not gst.element_link_many(source, spider, sink):
        print "ERROR: could not link"
        sys.exit (1)

    print 'Playing:', os.path.basename(filename)
    if not bin.set_state(gst.STATE_PLAYING):
        print "ERROR: could not set bin to playing"
        sys.exit (1)

    playing = 1
    while playing:
        try:
             if not bin.iterate():
                 playing = 0
        except KeyboardInterrupt:
            if not bin.set_state(gst.STATE_PAUSED):
                print "ERROR: could not set bin to paused"
                sys.exit (1)
	    print "Paused.  Press Enter to go back to playing."
            try:
                sys.stdin.readline ()
                if not bin.set_state(gst.STATE_PLAYING):
                    print "ERROR: could not set bin to playing"
                    sys.exit (1)
	        print "Playing."
            except KeyboardInterrupt:
                playing = 0 
    print "DONE playing"
    bin.set_state(gst.STATE_NULL)

def main(args):
    map(playfile, args[1:])

if __name__ == '__main__':
   sys.exit(main(sys.argv))

