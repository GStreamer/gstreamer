#!/usr/bin/env python
import sys
from unittest import TestSuite, TestLoader, TextTestRunner
from types import ClassType

loader = TestLoader()
testRunner = TextTestRunner()

test = TestSuite()
for name in ('element', 'interface', 'pipeline'):
    test.addTest(loader.loadTestsFromName(name))
testRunner.run(tests)
