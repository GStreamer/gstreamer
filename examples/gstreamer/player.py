import os
import sys

import gst

def found_tags(element, source, tags):
    print 'Artist:', tags.get('artist')
    print 'Title: ', tags.get('title')
    print 'Album: ', tags.get('album')        

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

