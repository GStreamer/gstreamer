from common import gst, unittest

class PadTest(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.parse_launch('fakesrc name=source ! fakesink')
        src = self.pipeline.get_by_name('source')
        self.sink = src.get_pad('src')
        
    def testQuery(self):
        assert self.sink.query(gst.QUERY_TOTAL, gst.FORMAT_BYTES) == -1
        assert self.sink.query(gst.QUERY_POSITION, gst.FORMAT_BYTES) == 0
        assert self.sink.query(gst.QUERY_POSITION, gst.FORMAT_TIME) == 0

if __name__ == "__main__":
    unittest.main()
        
