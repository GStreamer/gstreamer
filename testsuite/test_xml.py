from common import gst, unittest

class PadTest(unittest.TestCase):
        
    def testQuery(self):
        xml = gst.XML()
        xml.parse_memory("""<?xml version="1.0"?>
<gstreamer xmlns:gst="http://gstreamer.net/gst-core/1.0/">
  <gst:element>
    <gst:name>test-pipeline</gst:name>
    <gst:type>pipeline</gst:type>
    <gst:param>
      <gst:name>name</gst:name>
      <gst:value>test-pipeline</gst:value>
    </gst:param>
  </gst:element>
</gstreamer>""")
        elements = xml.get_topelements()
        assert len(elements) == 1
        element = elements[0]
        assert isinstance(element, gst.Pipeline)
        assert element.get_name() == 'test-pipeline'
        
if __name__ == "__main__":
    unittest.main()
        
