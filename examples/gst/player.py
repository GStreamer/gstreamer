import os
import sys

import gst

def found_tags(element, source, tags):
    for tag in tags.keys():
        print gst.tag_get_nick(tag), tags[tag]

def playfile(filename):
    bin = gst.Pipeline('player')
    
    source = gst.Element('filesrc', 'src')
    source.set_property('location', filename)

    spider = gst.Element('spider', 'spider')
    spider.connect('found-tag', found_tags)

    sink = gst.Element('osssink', 'sink')

    bin.add_many(source, spider, sink)
    gst.element_link_many(source, spider, sink)

    print 'Playing:', os.path.basename(filename)
    bin.set_state(gst.STATE_PLAYING)

    try:
        while bin.iterate():
            pass
    except KeyboardInterrupt:
        pass
    
    bin.set_state(gst.STATE_NULL)

def main(args):
    map(playfile, args[1:])

if __name__ == '__main__':
   sys.exit(main(sys.argv))

