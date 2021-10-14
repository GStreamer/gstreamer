#!/usr/bin/python3
import unittest
from pathlib import Path
from unittest import TestCase
from gi.repository import Gst


class TestBin(TestCase):
    def test_overrides(self):
        from gi.overrides import Gst
        self.assertEqual(Path(Gst.__file__), Path(__file__).parents[2] / "subprojects/gst-python/gi/overrides/Gst.py")

    def simple_functional_test(self):
        Gst.init(None)
        self.assertEqual(Gst.ElementFactory.make("bin", None).sinkpads, [])
        self.assertEqual(float(Gst.Fraction(1, 2)), 0.5)


if __name__ == "__main__":
    unittest.main()
