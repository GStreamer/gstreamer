from common import gst, unittest

class PipelineTest(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.Pipeline('test-pipeline')

        source = gst.Element('fakesrc', 'source')
        source.set_property('num-buffers', 5)
        sink = gst.Element('fakesink', 'sink')
        self.pipeline.add_many(source, sink)
        gst.element_link_many(source, sink)

    def testRun(self):
        self.assertEqual(self.pipeline.get_state(), gst.STATE_NULL)        
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.assertEqual(self.pipeline.get_state(), gst.STATE_PLAYING)
        
        while self.pipeline.iterate():
            pass

        self.assertEqual(self.pipeline.get_state(), gst.STATE_PAUSED)
        self.pipeline.set_state(gst.STATE_NULL)
        self.assertEqual(self.pipeline.get_state(), gst.STATE_NULL)
        
if __name__ == "__main__":
    unittest.main()
