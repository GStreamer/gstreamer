import os
import sys
from common import gst, unittest

class EventTest(unittest.TestCase):
    def setUp(self):
        pipeline = gst.parse_launch('fakesrc ! fakesink name=sink')
        self.sink = pipeline.get_by_name('sink')
        pipeline.set_state(gst.STATE_PLAYING)
        
    def testEventEmpty(self):
        event = gst.Event(gst.EVENT_EMPTY)
        self.sink.send_event(event)
        
    def testEventSeek(self):
        event = gst.event_new_seek(gst.SEEK_METHOD_CUR, 0)
        self.sink.send_event(event)

class EventFileSrcTest(unittest.TestCase):
    filename = '/tmp/gst-python-test-file'
    def setUp(self):
        if os.path.exists(self.filename):
            os.remove(self.filename)
        open(self.filename, 'w').write(''.join(map(str, range(10))))
                
        self.pipeline = gst.parse_launch('filesrc name=source location=%s blocksize=1 ! fakesink signal-handoffs=1 name=sink' % self.filename)
        self.source = self.pipeline.get_by_name('source')
        self.sink = self.pipeline.get_by_name('sink')
        self.sink.connect('handoff', self.handoff_cb)
        self.pipeline.set_state(gst.STATE_PLAYING)
        
    def tearDown(self):
        assert self.pipeline.set_state(gst.STATE_PLAYING)
        if os.path.exists(self.filename):
            os.remove(self.filename)

    def handoff_cb(self, element, buffer, pad):
        self.handoffs.append(str(buffer))

    def playAndIter(self):
        self.handoffs = []
        assert self.pipeline.set_state(gst.STATE_PLAYING)
        while self.pipeline.iterate():
            pass
        assert self.pipeline.set_state(gst.STATE_PAUSED)
        handoffs = self.handoffs
        self.handoffs = []
        return handoffs

    def sink_seek(self, offset, method=gst.SEEK_METHOD_SET):
        method |= (gst.SEEK_FLAG_FLUSH | gst.FORMAT_BYTES)
        self.source.send_event(gst.event_new_seek(method, offset))
        self.source.send_event(gst.Event(gst.EVENT_FLUSH)) 
        self.sink.send_event(gst.event_new_seek(method, offset))
        self.sink.send_event(gst.Event(gst.EVENT_FLUSH))
       
    def testSimple(self):
        handoffs = self.playAndIter()
        assert handoffs == map(str, range(10))
    
    def testSeekCur(self):
        self.sink_seek(8)
        
        #print self.playAndIter()
        
        

if __name__ == "__main__":
    unittest.main()
