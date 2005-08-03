import os
import sys

import tempfile

from common import gst, unittest

class ErrorTest(unittest.TestCase):
    def setUp(self):
        fd, self.name = tempfile.mkstemp(prefix='tmpgsttest')
        os.close(fd)
        os.unlink(self.name)

    def testFileSrc(self):
        pipe = gst.parse_launch("filesrc location=%s ! fakesink" % self.name)
        pipe.connect('error', self._error_cb)
        pipe.set_state(gst.STATE_PLAYING)
        pipe.iterate()
        self.failUnless(self.errored)
        self.assertEquals(self.element, pipe)
        # FIXME: GError should be wrapped in pygtk
        self.assertEquals(self.error.__class__, gst.GError)

    def _error_cb(self, element, source, error, debug):
        self.errored = True
        self.element = element
        self.source = source
        self.error = error
        self.debug = debug
        

if __name__ == "__main__":
    unittest.main()
