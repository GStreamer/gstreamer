#!/usr/bin/env python
import glob
import os
import sys
import unittest

SKIP_FILES = ['common', 'runtests']

def gettestnames():
    dir = os.path.split(os.path.abspath(__file__))[0]
    files = [os.path.basename(p) for p in glob.glob('%s/*.py' % dir)]
    names = map(lambda x: x[:-3], files)
    map(names.remove, SKIP_FILES)
    return names
        
suite = unittest.TestSuite()
loader = unittest.TestLoader()

for name in gettestnames():
    suite.addTest(loader.loadTestsFromName(name))
    
testRunner = unittest.TextTestRunner()
result = testRunner.run(suite)
if result.failures or result.errors:
    sys.exit(1)
