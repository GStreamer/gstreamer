import sys
from common import gst, unittest

class BufferTest(unittest.TestCase):
    def testBuffer(self):
        self.buffer = gst.Buffer('test')
        assert str(buffer(self.buffer)) == 'test'
        
if __name__ == "__main__":
    unittest.main()
