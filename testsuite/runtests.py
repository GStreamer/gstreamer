#!/usr/bin/env python
import glob
import sys
import unittest

SKIP_FILES = ['common', 'runtests']

def gettestnames():
    files = glob.glob('*.py')
    names = map(lambda x: x[:-3], files)
    map(names.remove, SKIP_FILES)
    return names
        
suite = unittest.TestSuite()
loader = unittest.TestLoader()

for name in gettestnames():
    suite.addTest(loader.loadTestsFromName(name))
    
testRunner = unittest.TextTestRunner()
testRunner.run(suite)
