#!/usr/bin/env python
import os
import sys

import gst

def found_tags_cb(element, source, tags):
    for tag in tags.keys():
        print "%s: %s" % (gst.tag_get_nick(tag), tags[tag])

def error_cb(*args):
    print args

def playfile(filename):
    bin = gst.Thread('player')
    bin.connect('eos', lambda x: gst.main_quit())
    bin.connect('error', error_cb)

    source = gst.Element('filesrc', 'src')
    source.set_property('location', filename)

    spider = gst.Element('spider', 'spider')
    spider.connect('found-tag', found_tags_cb)
    
    sink = gst.Element('osssink', 'sink')
    #sink.set_property('release-device', 1)

    bin.add_many(source, spider, sink)
    if not gst.element_link_many(source, spider, sink):
        print "ERROR: could not link"
        return 2

    print 'Playing:', os.path.basename(filename)
    if not bin.set_state(gst.STATE_PLAYING):
        print "ERROR: could not set bin to playing"
        return 2

    gst.main()

def main(args):
    if len(args) != 2:
        print 'Usage; player.py filename'
        return 1
    return playfile(args[1])

if __name__ == '__main__':
   sys.exit(main(sys.argv))

