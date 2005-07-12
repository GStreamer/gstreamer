from common import gst, unittest

class PipelineConstructor(unittest.TestCase):
    def testGoodConstructor(self):
        name = 'test-pipeline'
        pipeline = gst.Pipeline(name)
        assert pipeline is not None, 'pipeline is None'
        assert isinstance(pipeline, gst.Pipeline), 'pipeline is not a GstPipline'
        assert pipeline.get_name() == name, 'pipelines name is wrong'
        
## class ThreadConstructor(unittest.TestCase):
##     def testCreate(self):
##         thread = gst.Thread('test-thread')
##         assert thread is not None, 'thread is None'
##         assert isinstance(thread, gst.Thread)
        
class Pipeline(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.Pipeline('test-pipeline')
        source = gst.element_factory_make('fakesrc', 'source')
        source.set_property('num-buffers', 5)
        sink = gst.element_factory_make('fakesink', 'sink')
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
