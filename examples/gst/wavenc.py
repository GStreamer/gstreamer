#!/usr/bin/env python
import sys
import gst

def decode(filename):
    output = filename + '.wav'
    pipeline = ('{ filesrc location="%s" ! spider ! audio/x-raw-int,rate=44100,stereo=2 ! wavenc ! '
                'filesink location="%s" }') % (filename, output)
    
    bin = gst.parse_launch(pipeline)
    bin.set_state(gst.STATE_PLAYING)
    bin.connect('eos', lambda bin: gst.main_quit())
    gst.main()
    
def main(args):
    for arg in args[1:]:
        decode(arg)
        
if __name__ == '__main__':
    sys.exit(main(sys.argv))
