import sys
from common import gst, unittest

class EventTest(unittest.TestCase):
    def setUp(self):
        pipeline = gst.parse_launch('fakesrc ! fakesink name=sink')
        self.sink = pipeline.get_by_name('sink')
        
    def testEventEmpty(self):
        event = gst.Event(gst.EVENT_EMPTY)
        self.sink.send_event(event)
        
    def testEventSeek(self):
        event = gst.event_new_seek(gst.SEEK_METHOD_CUR, 0)
        self.sink.send_event(event)

if __name__ == "__main__":
    unittest.main()
