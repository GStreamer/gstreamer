import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent / "../../subprojects/gst-python/testsuite"))

import overrides_hack

overrides_hack

from common import TestCase, unittest
from gi.repository import Gst


class TestBin(TestCase):
    def test(self):
        Gst.init(None)
        self.assertEqual(Gst.ElementFactory.make("bin", None).sinkpads, [])
        self.assertEqual(float(Gst.Fraction(1, 2)), 0.5)


if __name__ == "__main__":
    unittest.main()
