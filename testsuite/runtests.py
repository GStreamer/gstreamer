#!/usr/bin/env python
import sys
from unittest import TestLoader, TextTestRunner
from types import ClassType

loader = TestLoader()
testRunner = TextTestRunner()

for name in ('element', 'pipeline'):
    print 'Testing', name
    tests = loader.loadTestsFromName(name)
    testRunner.run(tests)
