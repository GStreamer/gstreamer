import sys
import gobject
import gst

class FileSource(gst.Element):
    blocksize = 4096
    fd = None
    def __init__(self, name):
        self.__gobject_init__()
        self.set_name(name)
        self.srcpad = gst.Pad('src', gst.PAD_SRC)
        self.srcpad.set_get_function(self.srcpad_get)
        self.add_pad(self.srcpad)
            
    def set_property(self, name, value):
        if name == 'location':
            self.fd = open(value, 'r')
            
    def srcpad_get(self, pad):
        data = self.fd.read(self.blocksize)
        if data:
            return gst.Buffer(data)
        else:
            self.set_eos()
            return gst.Event(gst.EVENT_EOS)
gobject.type_register(FileSource)

def main(args):
    if len(args) != 3:
        print 'Usage: %s input output' % (args[0])
        return -1
    
    bin = gst.Pipeline('pipeline')

    filesrc = FileSource('filesource')
    #filesrc = gst.Element('filesrc', 'src')
    filesrc.set_property('location', args[1])
   
    filesink = gst.Element('filesink', 'sink')
    filesink.set_property('location', args[2])

    bin.add_many(filesrc, filesink)
    gst.element_link_many(filesrc, filesink)
    
    bin.set_state(gst.STATE_PLAYING);

    while bin.iterate():
        pass

    bin.set_state(gst.STATE_NULL)

if __name__ == '__main__':
   sys.exit(main(sys.argv))

